#include "vehicle.h"

#include "occupancy.h"
#include "sdk/game_sdk.h"
#include "sdk/patterns.h"
#include "sdk/ped/access.h"
#include "sdk/world/streaming.h"
#include "shared/vehicle/colors.h"

#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>

#include <CAutomobile.h>
#include <CBike.h>
#include <CDamageManager.h>
#include <CDoor.h>
#include <CFireManager.h>
#include <CPlane.h>
#include <CVehicleModelInfo.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>

namespace VantaMP::SDK {
    namespace {
        using CreateCarFn      = CVehicle *(__cdecl *)(int, CVector, unsigned char);
        using SetPosnFn        = void(__thiscall *)(void *, float, float, float);
        using UpdateRwMatrixFn = void(__thiscall *)(void *);
        using UpdateRwFrameFn  = void(__thiscall *)(void *);
        using SetOrientationFn = void(__thiscall *)(void *, float, float, float);
        using WorldRemoveFn    = void(__cdecl *)(void *);
        using WorldAddFn       = void(__cdecl *)(void *);

        // CVehicle::OpenDoor. MTA's 3-arg wrapper hits this with the node
        // table below; writing CDoor::m_fAngle alone does not move the mesh.
        constexpr std::size_t kOpenDoorSlot = 27;

        constexpr int kDoorNodeIndex[6] = {0x10, 0x11, 0x0A, 0x08, 0x0B, 0x09};
        constexpr int kDoorCount        = 6;
        constexpr int kPanelCount       = 7;
        // MTA CDamageManagerSA::GetCarNodeIndexFromPanel. Rear wings have
        // no node; those two are status-only.
        constexpr int kPanelNodeIndex[7] = {15, 14, -1, -1, 18, 12, 13};

        constexpr unsigned char kDoorIntact   = 0;
        constexpr unsigned char kDoorSwinging = 1;
        constexpr unsigned char kDoorDamaged  = 2;
        constexpr unsigned char kDoorMissing  = 4;

        using OpenDoorFn        = void(__thiscall *)(CVehicle *, CPed *, int, int, float, bool);
        using FixDoorFn         = void(__thiscall *)(CAutomobile *, int, eDoors);
        using FixPanelFn        = void(__thiscall *)(CAutomobile *, int, ePanels);
        using SetDoorDamageFn   = void(__thiscall *)(CAutomobile *, eDoors, bool);
        using SetPanelDamageFn  = void(__thiscall *)(CAutomobile *, ePanels, bool);
        using SetBumperDamageFn = void(__thiscall *)(CAutomobile *, ePanels, bool);

        constexpr float kSteerScale = 1.0f / 128.0f;
        constexpr float kPedalScale = 1.0f / 255.0f;

        CVehicle *AsVehicle(void *vehicle) {
            return static_cast<CVehicle *>(vehicle);
        }

        const CVehicle *AsVehicle(const void *vehicle) {
            return static_cast<const CVehicle *>(vehicle);
        }

        CMatrix *MatrixOf(CVehicle *vehicle) {
            return vehicle && vehicle->m_matrix ? vehicle->m_matrix : nullptr;
        }

        const CMatrix *MatrixOf(const CVehicle *vehicle) {
            return vehicle && vehicle->m_matrix ? vehicle->m_matrix : nullptr;
        }

        // GTA names `up` as forward and `at` as up. A quat built from the
        // English names would land a car on its roof the first time it pitched.
        //
        // RE-ORTHONORMALISED FIRST. glm::quat_cast assumes a pure rotation,
        // and a GTA vehicle matrix is not reliably one: the game lets small
        // scale and shear accumulate (a bike's is rebuilt through the lean
        // matrix every frame, which is the worst case). Feeding that in
        // directly yields a quaternion that is not quite the orientation the
        // car has, and once that quat is interpolated and written back the
        // error compounds - the tilt the driver never had, drifting.
        //
        // Gram-Schmidt against forward and up, which is what the game's own
        // CMatrix::Reorthogonalise does.
        glm::quat QuatFromMatrix(const CMatrix &matrix) {
            const glm::vec3 forward(matrix.up.x, matrix.up.y, matrix.up.z);
            const glm::vec3 up(matrix.at.x, matrix.at.y, matrix.at.z);

            const float forwardLength = glm::length(forward);
            const float upLength      = glm::length(up);

            if (forwardLength < 1e-6f || upLength < 1e-6f) {
                return glm::identity<glm::quat>();
            }

            const glm::vec3 f = forward / forwardLength;
            glm::vec3       r = glm::cross(f, up / upLength);
            const float     rLength = glm::length(r);

            // Forward and up parallel: degenerate, nothing to build a basis
            // from. Keep what the car already has rather than inventing a roll.
            if (rLength < 1e-6f) {
                return glm::identity<glm::quat>();
            }

            r /= rLength;

            const glm::vec3 u = glm::cross(r, f);

            return glm::normalize(glm::quat_cast(glm::mat3(r, f, u)));
        }

        void ApplyQuatToMatrix(CMatrix &matrix, const glm::quat &rotation) {
            const glm::mat3 axes = glm::mat3_cast(glm::normalize(rotation));

            matrix.right = CVector(axes[0].x, axes[0].y, axes[0].z);
            matrix.up    = CVector(axes[1].x, axes[1].y, axes[1].z);
            matrix.at    = CVector(axes[2].x, axes[2].y, axes[2].z);
        }

        CDamageManager *DamageManagerOf(CVehicle *vehicle) {
            if (!vehicle) {
                return nullptr;
            }

            switch (vehicle->m_nVehicleSubClass) {
            case VEHICLE_AUTOMOBILE:
            case VEHICLE_MTRUCK:
            case VEHICLE_QUAD:
            case VEHICLE_HELI:
            case VEHICLE_PLANE:
            case VEHICLE_TRAILER:
                return &static_cast<CAutomobile *>(vehicle)->m_damageManager;

            default:
                return nullptr;
            }
        }

        std::uint64_t PackDoors(const unsigned char *doors) {
            std::uint64_t packed = 0;

            for (int i = 0; i < 6; ++i) {
                packed |= static_cast<std::uint64_t>(doors[i]) << (i * 8);
            }

            return packed;
        }

        std::uint32_t PackWheels(const unsigned char *wheels) {
            std::uint32_t packed = 0;

            for (int i = 0; i < 4; ++i) {
                packed |= static_cast<std::uint32_t>(wheels[i]) << (i * 8);
            }

            return packed;
        }

        void UnpackDoors(std::uint64_t packed, unsigned char *doors) {
            for (int i = 0; i < 6; ++i) {
                doors[i] = static_cast<unsigned char>((packed >> (i * 8)) & 0xFFu);
            }
        }

        void UnpackWheels(std::uint32_t packed, unsigned char *wheels) {
            for (int i = 0; i < 4; ++i) {
                wheels[i] = static_cast<unsigned char>((packed >> (i * 8)) & 0xFFu);
            }
        }

        CAutomobile *AsAutomobile(CVehicle *vehicle) {
            if (!vehicle) {
                return nullptr;
            }

            switch (vehicle->m_nVehicleSubClass) {
            case VEHICLE_AUTOMOBILE:
            case VEHICLE_MTRUCK:
            case VEHICLE_QUAD:
            case VEHICLE_HELI:
            case VEHICLE_PLANE:
            case VEHICLE_TRAILER:
                return static_cast<CAutomobile *>(vehicle);

            default:
                return nullptr;
            }
        }

        float DoorOpenRatioOf(const CDoor &door) {
            const float span = door.m_fOpenAngle - door.m_fClosedAngle;

            if (std::fabs(span) < 0.0001f) {
                return 0.0f;
            }

            return std::clamp((door.m_fAngle - door.m_fClosedAngle) / span, 0.0f, 1.0f);
        }

        std::uint64_t PackDoorRatios(const CAutomobile *car) {
            std::uint64_t packed = 0;

            if (!car) {
                return packed;
            }

            for (int i = 0; i < kDoorCount; ++i) {
                const auto byte = static_cast<std::uint8_t>(std::clamp(DoorOpenRatioOf(car->m_doors[i]), 0.0f, 1.0f) * 255.0f);

                packed |= static_cast<std::uint64_t>(byte) << (i * 8);
            }

            return packed;
        }

        bool DoorRatioIsOwnedByGame(unsigned char status) {
            // Swinging / missing: GTA's door physics owns the angle.
            // Forcing OpenDoor here is the stutter on a hanging door.
            return status == kDoorSwinging || status == (kDoorDamaged | kDoorSwinging) || status >= kDoorMissing;
        }

        void OpenPackedDoors(CVehicle *vehicle, std::uint64_t packed, std::uint8_t skipMask) {
            auto *vtable = *reinterpret_cast<OpenDoorFn **>(vehicle);
            auto *car    = AsAutomobile(vehicle);
            auto *damage = DamageManagerOf(vehicle);

            if (!vtable || !car) {
                return;
            }

            for (int i = 0; i < kDoorCount; ++i) {
                if ((skipMask & static_cast<std::uint8_t>(1u << i)) != 0) {
                    continue;
                }

                if (damage && DoorRatioIsOwnedByGame(damage->m_anDoorsStatus[i])) {
                    continue;
                }

                const float ratio = static_cast<float>((packed >> (i * 8)) & 0xFFu) / 255.0f;

                // ALREADY THERE: do not call OpenDoor. That function plays
                // the slam even with playSound false, and doing it every
                // frame is the kick-and-clap loop.
                if (std::fabs(DoorOpenRatioOf(car->m_doors[i]) - ratio) < (2.0f / 255.0f)) {
                    continue;
                }

                vtable[kOpenDoorSlot](vehicle, nullptr, kDoorNodeIndex[i], i, ratio, false);
            }
        }

        unsigned char PanelNibble(std::uint32_t packed, int index) {
            return static_cast<unsigned char>((packed >> (index * 4)) & 0x0Fu);
        }

        std::int8_t SteerToWire(float angle) {
            // Negated: stick-right is a negative steer angle in GTA.
            const float scaled = std::clamp(-angle / kSteerScale, -128.0f, 127.0f);

            return static_cast<std::int8_t>(scaled);
        }

        std::uint8_t PedalToWire(float pedal) {
            return static_cast<std::uint8_t>(std::clamp(pedal / kPedalScale, 0.0f, 255.0f));
        }

        // SIGNED, BECAUSE m_fGasPedal IS.
        //
        // It runs -1..1 with negative meaning reverse (PostEffects.cpp:968
        // biases it as `m_GasPedal + 1.0f` to get 0..2). PedalToWire above
        // clamps the low end to zero, which throws reverse away entirely.
        std::int8_t SignedPedalToWire(float pedal) {
            return static_cast<std::int8_t>(std::clamp(pedal * 127.0f, -127.0f, 127.0f));
        }

        void ExtinguishFire(CFire *fire) {
            if (!fire || !gPatterns.CFire__Extinguish) {
                return;
            }

            reinterpret_cast<void(__thiscall *)(CFire *)>(gPatterns.CFire__Extinguish)(fire);
        }

        void ExtinguishCarFire(CVehicle *vehicle) {
            if (!vehicle) {
                return;
            }

            // MTA's vehicle destructor. Safer than walking CFire slots:
            // Extinguish on a wreck fire whose FxSystem is already gone
            // is how disconnect after a boom used to crash.
            if (gPatterns.CVehicle__ExtinguishCarFire) {
                reinterpret_cast<void(__thiscall *)(CVehicle *)>(gPatterns.CVehicle__ExtinguishCarFire)(vehicle);
                return;
            }

            ExtinguishFire(vehicle->m_pFire);
        }

        void KeepVehicleResident(CVehicle *vehicle) {
            if (!vehicle) {
                return;
            }

            // CreateCarForScript sets these. BlowUpCar and the streamer
            // clear them, and then GTA treats the wreck as disposable:
            // CanBeDeleted, RemoveFallenCars, fade after 60 seconds.
            vehicle->bIsLocked                    = true;
            vehicle->bNeverUseSmallerRemovalRange = true;
            vehicle->bFadeOut                     = false;
            vehicle->bHasBeenOwnedByPlayer        = true;
            vehicle->m_nCreatedBy                 = MISSION_VEHICLE;
            vehicle->m_nTimeTillWeNeedThisCar     = 0x7FFFFFFFu;
            vehicle->m_nTimeWhenBlowedUp          = 0x7FFFFFFFu;

            vehicle->bStreamingDontDelete = true;
            vehicle->bDontStream          = false;
            vehicle->bDistanceFade        = false;
            vehicle->bRemoveFromWorld     = false;
            vehicle->bIsVisible           = true;
        }

        void DeleteFromVehiclePool(CVehicle *vehicle) {
            auto *pool = *reinterpret_cast<CPool<CVehicle, CHeli> **>(gPatterns.CPools__ms_pVehiclePool);

            if (!pool || !vehicle) {
                return;
            }

            pool->Delete(vehicle);
        }

        // CEntity::ProcessControl. plugin-sdk lists it after GetBoundRect;
        // the live vtable slot is 10, which is what old Vanta and
        // CoopAndreas hook for the same pad swap.
        constexpr std::size_t kProcessControlSlot = 10;

        // CVehicle::BlowUpCar. plugin-sdk reaches it as vtable[41].
        constexpr std::size_t kBlowUpCarSlot = 41;

        using ProcessControlFn = void(__thiscall *)(void *);
        using BlowUpCarFn      = void(__thiscall *)(CVehicle *, CEntity *, bool);
        using KillPedsFn       = void(__thiscall *)(CVehicle *);

        std::unordered_map<void **, ProcessControlFn> gOriginalProcessControl;
        std::unordered_map<void **, BlowUpCarFn>      gOriginalBlowUpCar;

        KillPedsFn gOriginalKillPedsIn        = nullptr;
        KillPedsFn gOriginalKillPedsGettingIn = nullptr;

        using SetupRenderFn = void(__thiscall *)(CVehicle *);

        SetupRenderFn gOriginalSetupRender = nullptr;

        struct VehicleRgb {
            std::uint8_t r[Shared::kVehicleColorCount] {};
            std::uint8_t g[Shared::kVehicleColorCount] {};
            std::uint8_t b[Shared::kVehicleColorCount] {};
        };

        struct PaletteRgba {
            std::uint8_t r;
            std::uint8_t g;
            std::uint8_t b;
            std::uint8_t a;
        };

        std::unordered_map<const void *, VehicleRgb> gVehicleRgb;

        PaletteRgba *ColourTable() {
            return reinterpret_cast<PaletteRgba *>(gPatterns.CVehicleModelInfo__ms_vehicleColourTable);
        }

        char *CompsToUse() {
            return reinterpret_cast<char *>(gPatterns.CVehicleModelInfo__ms_compsToUse);
        }

        std::uint8_t CompByte(std::uint8_t variant) {
            // 255 = script "default" → GTA's random sentinel 0xFE.
            return variant == 255 ? 0xFE : variant;
        }

        void __fastcall SetupRender_Hook(CVehicle *vehicle) {
            auto *table = ColourTable();
            const VehicleRgb *rgb = nullptr;
            PaletteRgba saved[Shared::kVehicleColorCount] {};

            if (vehicle) {
                const auto found = gVehicleRgb.find(vehicle);

                if (found != gVehicleRgb.end()) {
                    rgb = &found->second;
                }
            }

            if (rgb && table) {
                // Four independent RGB slots. The vehicle's own indexes
                // would collapse two colours that happened to share a
                // palette entry.
                vehicle->m_nPrimaryColor     = 0;
                vehicle->m_nSecondaryColor   = 1;
                vehicle->m_nTertiaryColor    = 2;
                vehicle->m_nQuaternaryColor  = 3;

                for (int i = 0; i < Shared::kVehicleColorCount; ++i) {
                    saved[i]   = table[i];
                    table[i].r = rgb->r[i];
                    table[i].g = rgb->g[i];
                    table[i].b = rgb->b[i];
                    table[i].a = 255;
                }
            }

            gOriginalSetupRender(vehicle);

            if (rgb && table) {
                for (int i = 0; i < Shared::kVehicleColorCount; ++i) {
                    table[i] = saved[i];
                }
            }
        }

        // Seated in this car, in any seat that is not the wheel. Used to
        // decide whether the local pad has to be neutralised before GTA
        // processes a car somebody else is driving.
        bool LocalPedIsPassengerOf(CVehicle *vehicle) {
            if (!vehicle) {
                return false;
            }

            const auto *local = Internal::LocalPed();

            if (!local || vehicle->m_pDriver == local) {
                return false;
            }

            for (const auto *passenger : vehicle->m_apPassengers) {
                if (passenger == local) {
                    return true;
                }
            }

            return false;
        }

        int SlotOfRemoteDriver(CPed *driver) {
            if (!driver || driver == Internal::LocalPed()) {
                return -1;
            }

            for (int slot = 2; slot < SDK::kMaxPlayers; ++slot) {
                if (Internal::PedAt(slot) == driver) {
                    return slot;
                }
            }

            return -1;
        }

        void __fastcall VehicleProcessControl_Hook(CVehicle *vehicle, void * /*edx*/) {
            void **vtable = *reinterpret_cast<void ***>(vehicle);
            const auto found = gOriginalProcessControl.find(vtable);

            if (found == gOriginalProcessControl.end()) {
                return;
            }

            const int slot = vehicle ? SlotOfRemoteDriver(vehicle->m_pDriver) : -1;

            // THE PAD THIS CAR IS PROCESSED AGAINST IS THE DRIVER'S, OR NONE.
            //
            // ProcessControl reads GetPad(0). Swapping in the remote driver's
            // pad is how a remote car steers - but when the driver is NOT a
            // ped we own a slot for (mid-jack, a driver still streaming in, a
            // driver who just left), slot is -1 and the game would fall
            // through to the raw local pad. If the local player is sitting in
            // that same car as a PASSENGER, their own steering and pedals are
            // then what GTA drives the car with: the passenger steers the
            // driver's vehicle. That is the authority bug, and it is a
            // physical one - it happens inside GTA, below any packet we send.
            //
            // A neutral pad is the fix: the car keeps its physics and its
            // momentum, and simply receives no input this frame, which is
            // what "somebody else is driving" should look like locally.
            const bool localIsNotDriver = vehicle && vehicle->m_pDriver != Internal::LocalPed();

            void *localPad  = nullptr;
            void *remotePad = nullptr;
            std::array<std::uint8_t, SDK::kPadSize> saved {};
            bool neutralised = false;

            if (slot >= 0) {
                localPad  = reinterpret_cast<Internal::GetPadFn>(gPatterns.CPad__GetPad)(0);
                remotePad = SDK::PadAt(slot);

                if (localPad && remotePad) {
                    std::memcpy(saved.data(), localPad, SDK::kPadSize);
                    std::memcpy(localPad, remotePad, SDK::kPadSize);
                }
            }
            else if (localIsNotDriver && LocalPedIsPassengerOf(vehicle)) {
                localPad = reinterpret_cast<Internal::GetPadFn>(gPatterns.CPad__GetPad)(0);

                if (localPad) {
                    std::memcpy(saved.data(), localPad, SDK::kPadSize);
                    std::memset(localPad, 0, SDK::kPadSize);
                    neutralised = true;
                }
            }

            found->second(vehicle);

            if (localPad && remotePad) {
                std::memcpy(remotePad, localPad, SDK::kPadSize);
                std::memcpy(localPad, saved.data(), SDK::kPadSize);
            }
            else if (neutralised && localPad) {
                std::memcpy(localPad, saved.data(), SDK::kPadSize);
            }
        }

        void __fastcall BlowUpCar_Hook(CVehicle *vehicle, void * /*edx*/, CEntity *damager, bool hideExplosion) {
            void **vtable = vehicle ? *reinterpret_cast<void ***>(vehicle) : nullptr;
            const auto found = vtable ? gOriginalBlowUpCar.find(vtable) : gOriginalBlowUpCar.end();

            if (found == gOriginalBlowUpCar.end()) {
                return;
            }

            // Empty the seats before GTA posts CEventVehicleDied. That
            // event treats a Vanta CPlayerPed as an NPC or the single
            // local player, and either reading is a crash. The occupants
            // are already marked dead so CheckLocalDeath still fires.
            EvacuateVehicle(vehicle, true);
            found->second(vehicle, damager, hideExplosion);
        }

        void __fastcall KillPedsInVehicle_Hook(CVehicle *vehicle, void * /*edx*/) {
            // THIS is what a tank shot reaches. BlowUpCar is not always
            // taken through the vtable we patch; this function is.
            EvacuateVehicle(vehicle, true);

            if (gOriginalKillPedsIn) {
                gOriginalKillPedsIn(vehicle);
            }
        }

        void __fastcall KillPedsGettingInVehicle_Hook(CVehicle *vehicle, void * /*edx*/) {
            // Walks the whole ped pool and posts CEventDamage on anyone
            // with an enter task. Vanta CPlayerPeds live in that pool.
            CancelEntriesInto(vehicle);
        }
    } // namespace

    bool IsVehicleModel(int modelId) {
        return modelId >= kFirstVehicleModel && modelId <= kLastVehicleModel;
    }

    void *CreateScriptVehicle(int modelId, float x, float y, float z, float heading, std::uint8_t primaryColour, std::uint8_t secondaryColour,
                              std::uint8_t variant, std::uint8_t variant2) {
        if (!IsVehicleModel(modelId)) {
            return nullptr;
        }

        if (auto *comps = CompsToUse()) {
            comps[0] = static_cast<char>(CompByte(variant));
            comps[1] = static_cast<char>(CompByte(variant2));
        }

        auto *vehicle = reinterpret_cast<CreateCarFn>(gPatterns.CCarCtrl__CreateCarForScript)(modelId, CVector(x, y, z), 0);

        if (auto *comps = CompsToUse()) {
            comps[0] = static_cast<char>(0xFE);
            comps[1] = static_cast<char>(0xFE);
        }

        if (!vehicle) {
            return nullptr;
        }

        reinterpret_cast<SetOrientationFn>(gPatterns.CPlaceable__SetOrientation)(vehicle, 0.0f, 0.0f, heading);

        vehicle->m_nPrimaryColor   = primaryColour;
        vehicle->m_nSecondaryColor = secondaryColour;

        // CreateCarForScript locks the doors: it is written for mission props.
        // vehicleFlags.bIsLocked is a deletion guard and stays set.
        vehicle->m_eDoorLock = DOORLOCK_UNLOCKED;
        KeepVehicleResident(vehicle);

        return vehicle;
    }

    void DestroyScriptVehicle(void *handle) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle) {
            return;
        }

        // Occupants first. CWorld::Remove walks the driver/passenger
        // pointers, and a Vanta CPlayerPed still sitting here is the
        // disconnect crash. Dead or alive does not matter - pointer
        // detach only.
        EvacuateVehicle(vehicle);
        ExtinguishCarFire(vehicle);
        Internal::ReleaseRealTimeShadow(vehicle);
        ClearVehicleRgbColors(vehicle);
        ClearVehicleHeadlightColor(vehicle);
        reinterpret_cast<WorldRemoveFn>(gPatterns.CWorld__Remove)(vehicle);
        DeleteFromVehiclePool(vehicle);
    }

    bool IsVehicleWrecked(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        return vehicle && vehicle->m_nStatus == STATUS_WRECKED;
    }

    void KeepScriptVehicle(void *handle) {
        KeepVehicleResident(AsVehicle(handle));
    }

    void WreckVehicle(void *handle, bool hideExplosion) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle) {
            return;
        }

        if (!IsVehicleWrecked(vehicle)) {
            vehicle->m_fHealth = 0.0f;

            // Through the live vtable so the BlowUpCar hook still empties
            // the seats. hideExplosion is a late join: the boom already
            // happened on the other clients.
            auto **vtable = *reinterpret_cast<void ***>(vehicle);

            reinterpret_cast<BlowUpCarFn>(vtable[kBlowUpCarSlot])(vehicle, nullptr, hideExplosion);
        }

        // BlowUpCar clears the lock and stamps m_nTimeWhenBlowedUp.
        // Without this the hull fades ~60 seconds later.
        KeepVehicleResident(vehicle);
    }

    void ClearTransientWorldEffects() {
        // MTA ~CClientGame: explosions first, then projectile refs,
        // then fires. Do not walk CFire slots - ExtinguishPoint is
        // the manager API and ExtinguishCarFire is per wreck.
        if (gPatterns.CExplosion__ClearAllExplosions) {
            reinterpret_cast<void (*)()>(gPatterns.CExplosion__ClearAllExplosions)();
        }

        if (gPatterns.CProjectileInfo__RemoveAllProjectiles) {
            reinterpret_cast<void (*)()>(gPatterns.CProjectileInfo__RemoveAllProjectiles)();
        }

        if (gPatterns.gFireManager && gPatterns.CFireManager__ExtinguishPoint) {
            reinterpret_cast<void(__thiscall *)(void *, CVector, float)>(gPatterns.CFireManager__ExtinguishPoint)(
                reinterpret_cast<void *>(gPatterns.gFireManager), CVector(0.0f, 0.0f, 0.0f), 20000.0f);
        }
    }

    VehicleState ReadVehicle(const void *handle) {
        VehicleState state;

        const auto *vehicle = AsVehicle(handle);

        if (!vehicle) {
            return state;
        }

        const auto &position = vehicle->GetPosition();

        state.x = position.x;
        state.y = position.y;
        state.z = position.z;

        state.vx = vehicle->m_vecMoveSpeed.x;
        state.vy = vehicle->m_vecMoveSpeed.y;
        state.vz = vehicle->m_vecMoveSpeed.z;

        if (const auto *matrix = MatrixOf(vehicle)) {
            state.rotation = QuatFromMatrix(*matrix);
        }

        state.avx = vehicle->m_vecTurnSpeed.x;
        state.avy = vehicle->m_vecTurnSpeed.y;
        state.avz = vehicle->m_vecTurnSpeed.z;

        state.health   = vehicle->m_fHealth;
        state.engineOn = vehicle->bEngineOn;
        state.sirenOn  = vehicle->bSirenOrAlarm;

        state.steer       = SteerToWire(vehicle->m_fSteerAngle);
        state.accelerator = PedalToWire(vehicle->m_fGasPedal);
        state.brake       = PedalToWire(vehicle->m_fBreakPedal);

        // The signed copies. Kept alongside the unsigned pair rather than
        // replacing it, so the existing car path and its wire layout are
        // untouched.
        state.gasPedal   = SignedPedalToWire(vehicle->m_fGasPedal);
        state.brakePedal = SignedPedalToWire(vehicle->m_fBreakPedal);
        state.handbrake   = vehicle->bIsHandbrakeOn;

        if (auto *damage = DamageManagerOf(const_cast<CVehicle *>(vehicle))) {
            state.damageEngine = damage->m_nEngineStatus;
            state.doorsPacked  = PackDoors(damage->m_anDoorsStatus);
            state.wheelsPacked = PackWheels(damage->m_anWheelsStatus);
            state.panels       = damage->m_nPanelsStatus;
            state.lights       = damage->m_nLightsStatus;
            state.hasDamage    = true;
        }

        state.doorRatiosPacked = PackDoorRatios(AsAutomobile(const_cast<CVehicle *>(vehicle)));

        return state;
    }

    void ApplyVehicleMotion(void *handle, const VehicleState &state) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle) {
            return;
        }

        vehicle->m_vecMoveSpeed = CVector(state.vx, state.vy, state.vz);
        vehicle->m_vecTurnSpeed = CVector(state.avx, state.avy, state.avz);
        vehicle->m_fHealth      = state.health;
        vehicle->bEngineOn      = state.engineOn;
        vehicle->bSirenOrAlarm  = state.sirenOn;
    }

    // STATES ONLY. NO MOMENTUM.
    //
    // ApplyVehicleMotion above always writes m_vecMoveSpeed and
    // m_vecTurnSpeed, so the read-modify-write idiom - read the vehicle,
    // change health, write it all back - silently re-imposes momentum too.
    // The values read came from the TOP of the sync tick, before GTA ran its
    // physics for this frame, so writing them back hands the vehicle a
    // one-frame-old velocity every frame. A car absorbs that; a bike
    // integrating two suspension points against it does not.
    //
    // Anything that only wants to change health / engine / siren must use
    // this instead.
    void ApplyVehicleStates(void *handle, float health, bool engineOn, bool sirenOn) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle) {
            return;
        }

        vehicle->m_fHealth     = health;
        vehicle->bEngineOn     = engineOn;
        vehicle->bSirenOrAlarm = sirenOn;
    }

    void ApplyVehicleDamage(void *handle, std::uint8_t engine, std::uint64_t doorsPacked, std::uint32_t wheelsPacked, std::uint32_t panels, std::uint32_t lights,
                            bool spawnFlying) {
        auto *vehicle = AsVehicle(handle);
        auto *damage  = DamageManagerOf(vehicle);
        auto *car     = AsAutomobile(vehicle);

        if (!damage || !car) {
            return;
        }

        unsigned char oldDoors[6] {};
        unsigned char newDoors[6] {};
        unsigned char newWheels[4] {};

        std::memcpy(oldDoors, damage->m_anDoorsStatus, sizeof(oldDoors));
        const auto oldPanels = damage->m_nPanelsStatus;

        UnpackDoors(doorsPacked, newDoors);
        UnpackWheels(wheelsPacked, newWheels);

        damage->m_nEngineStatus = engine;
        std::memcpy(damage->m_anDoorsStatus, newDoors, sizeof(newDoors));
        std::memcpy(damage->m_anWheelsStatus, newWheels, sizeof(newWheels));
        damage->m_nPanelsStatus = panels;
        damage->m_nLightsStatus = lights;

        // MTA SetDoorStatus / SetPanelStatus: FixDoor when it comes back,
        // SetDoorDamage only for the doors that changed. SetupDamageAfterLoad
        // on a live car respawns every missing part as a flying component.
        const bool quiet = !spawnFlying || vehicle->m_fHealth <= 250.0f;

        for (int i = 0; i < kDoorCount; ++i) {
            if (oldDoors[i] == newDoors[i]) {
                continue;
            }

            const auto door = static_cast<eDoors>(i);

            if (newDoors[i] == kDoorIntact || newDoors[i] == kDoorSwinging) {
                reinterpret_cast<FixDoorFn>(gPatterns.CAutomobile__FixDoor)(car, kDoorNodeIndex[i], door);
            }
            else {
                reinterpret_cast<SetDoorDamageFn>(gPatterns.CAutomobile__SetDoorDamage)(car, door, quiet);
            }
        }

        for (int i = 0; i < kPanelCount; ++i) {
            if (PanelNibble(oldPanels, i) == PanelNibble(panels, i)) {
                continue;
            }

            const auto panel = static_cast<ePanels>(i);

            if (i == BUMP_FRONT || i == BUMP_REAR) {
                reinterpret_cast<SetBumperDamageFn>(gPatterns.CAutomobile__SetBumperDamage)(car, panel, quiet);
                continue;
            }

            if (kPanelNodeIndex[i] < 0) {
                continue;
            }

            if (PanelNibble(panels, i) == 0) {
                reinterpret_cast<FixPanelFn>(gPatterns.CAutomobile__FixPanel)(car, kPanelNodeIndex[i], panel);
            }
            else {
                reinterpret_cast<SetPanelDamageFn>(gPatterns.CAutomobile__SetPanelDamage)(car, panel, false);
            }
        }
    }

    void ApplyVehicleDoorRatios(void *handle, std::uint64_t doorRatiosPacked, std::uint8_t skipMask) {
        auto *vehicle = AsVehicle(handle);

        // The owner closes doors on their own client. Writing OpenDoor over
        // that every tick is the close-spam: the game starts to shut them
        // and this puts them back.
        if (!vehicle || !AsAutomobile(vehicle) || IsLocalPlayerDriving(vehicle)) {
            return;
        }

        // AFTER SetupDamageAfterLoad. That rebuilds missing panels from the
        // status bytes and would slam a popped hood shut if we opened it first.
        OpenPackedDoors(vehicle, doorRatiosPacked, skipMask);
    }

    // FORCES ONE DOOR TO AN ANGLE, IGNORING THE OCCUPANCY MASK.
    //
    // The mask exists to stop per-packet ratio writes fighting an animation
    // that owns the door. This is the opposite case: an authoritative,
    // one-shot angle that arrives with the abort itself, for a door that is
    // no longer being animated by anyone. MTA does the same thing in the same
    // situation - AllowDoorRatioSetting(door, true) immediately followed by
    // SetDoorOpenRatio(door, ratio, 0, true), the `true` being its forced
    // flag (CPacketHandler.cpp:1972-1974).
    void ForceVehicleDoorRatio(void *handle, std::uint8_t doorIndex, std::uint8_t ratio) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || doorIndex >= kDoorCount) {
            return;
        }

        auto *vtable = *reinterpret_cast<OpenDoorFn **>(vehicle);
        auto *car    = AsAutomobile(vehicle);

        if (!vtable || !car) {
            return;
        }

        auto *damage = DamageManagerOf(vehicle);

        // Still respect a door GTA's own physics owns - swinging or missing.
        // Forcing those is the stutter the ratio path already avoids.
        if (damage && DoorRatioIsOwnedByGame(damage->m_anDoorsStatus[doorIndex])) {
            return;
        }

        const float wanted = static_cast<float>(ratio) / 255.0f;

        if (std::fabs(DoorOpenRatioOf(car->m_doors[doorIndex]) - wanted) < (2.0f / 255.0f)) {
            return;
        }

        vtable[kOpenDoorSlot](vehicle, nullptr, kDoorNodeIndex[doorIndex], doorIndex, wanted, false);
    }

    void ApplyVehicleControls(void *handle, std::int8_t steer, std::uint8_t accelerator, std::uint8_t brake) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || IsLocalPlayerDriving(vehicle)) {
            return;
        }

        vehicle->m_fSteerAngle = -static_cast<float>(steer) * kSteerScale;
        vehicle->m_fGasPedal   = static_cast<float>(accelerator) * kPedalScale;
        vehicle->m_fBreakPedal = static_cast<float>(brake) * kPedalScale;
    }

    // A REMOTE BIKE NEVER RUNS ProcessControlInputs, SO ITS OUTPUTS MUST BE
    // COPIED FROM THE WIRE.
    //
    // For a car the steer angle really is an output: GTA rebuilds it from
    // GetPad(0) every frame, which is why writing it used to be pointless.
    // A ridden remote BIKE does not get that far. Runtime trace, pad clearly
    // live and the owner clearly steering:
    //
    //   PAD AT PROCESSCTRL  stickX128 ... RS1(handbrake)0 triangle(exit)0
    //   AFTER GTA PROCESSCTRL  steer 0.00  hb1
    //   WIRE PACKET            steer60
    //
    // m_fSteerAngle is assigned in exactly one place - inside
    // ProcessControlInputs (CAutomobile.cpp:2037,
    // `m_fSteerAngle = SteeringLock * signedSquare(m_fRawSteerAngle)`), and
    // CBike's copy of that function is 0x6BE310. It plainly is not running
    // for this vehicle: the stick is at full lock and the angle stays 0.00.
    // The same function is the only thing that clears bIsHandbrakeOn, which
    // CVehicle's constructor sets true (Vehicle.cpp:253) - which is why the
    // handbrake never lets go either. One missing call, both symptoms.
    //
    // So the values it would have produced are taken from the owner, who
    // computed them in the only place GTA computes them - their own
    // ProcessControlInputs - and already sends them. Steer drives the front
    // fork and wheel visuals; the handbrake flag gates braking behaviour.
    //
    // THE PEDALS BELONG IN HERE TOO, AND USED NOT TO BE.
    //
    // The old comment said "pedals are left to the pad, which does reach the
    // engine". That is the same mistake steer was: the pad reaches the
    // engine, but it does NOT reach m_fGasPedal, because the only function
    // that converts pad buttons into that field is the one established above
    // as never running. Measured with reverse plainly held on the owner:
    //
    //   PAD AT PROCESSCTRL  ... cross0 square255 ...   (reverse, arriving)
    //   m_fGasPedal         0.00                       (never written)
    //
    // The rider's animation is selected from the pedal, so a remote rider
    // stayed in the idle pose while reversing instead of looking back over
    // their shoulder.
    void ApplyBikeDriverOutputs(void *handle, std::int8_t steer, bool handbrake, std::int8_t gasPedal, std::int8_t brakePedal) {
        auto *vehicle = AsVehicle(handle);

        // ONLY WHILE SOMEBODY IS ACTUALLY RIDING IT.
        //
        // A driverless bike is GTA's business: it wants the handbrake ON (the
        // parked branch of ProcessControl sets it, and the constructor
        // defaults it true), and clearing it from here just fought the game
        // every frame - hb went 1 -> 0 -> 1 sixty times a second on a bike
        // nobody was sitting on, which is churn we do not want and had no
        // authoritative brake state behind it anyway.
        if (!vehicle || IsLocalPlayerDriving(vehicle) || !IsBikeHandle(vehicle) || !vehicle->m_pDriver) {
            return;
        }

        vehicle->m_fSteerAngle = -static_cast<float>(steer) * kSteerScale;

        // Straight from the owner's own bIsHandbrakeOn. It was WRONG to
        // derive this from `brake`: that field is m_fBreakPedal, the foot
        // brake, so ordinary braking would have engaged the handbrake on the
        // remote copy - a bug this fix would have introduced.
        vehicle->bIsHandbrakeOn = handbrake;

        // Signed, so reverse survives. kSignedPedalScale is the inverse of
        // the 127 the sender scaled by.
        constexpr float kSignedPedalScale = 1.0f / 127.0f;

        vehicle->m_fGasPedal   = static_cast<float>(gasPedal) * kSignedPedalScale;
        vehicle->m_fBreakPedal = static_cast<float>(brakePedal) * kSignedPedalScale;
    }

    // POSITION ONLY, LEAVING THE MATRIX BASIS ALONE.
    //
    // For a ridden remote bike the orientation is GTA's between packets, so
    // the full pose write would re-stamp the identical basis and drag a
    // CWorld::Remove/Add plus an RW frame rebuild through every frame for no
    // change at all. That churn is visible as jitter. CPlaceable::SetPosn
    // moves the entity without touching right/forward/up, and the sector
    // re-registration still happens because the position is what changed.
    void SetVehiclePosition(void *handle, float x, float y, float z) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || !vehicle->m_matrix) {
            return;
        }

        reinterpret_cast<WorldRemoveFn>(gPatterns.CWorld__Remove)(vehicle);
        reinterpret_cast<SetPosnFn>(gPatterns.CPlaceable__SetPosn)(vehicle, x, y, z);
        reinterpret_cast<UpdateRwMatrixFn>(gPatterns.CEntity__UpdateRwMatrix)(vehicle);
        reinterpret_cast<UpdateRwFrameFn>(gPatterns.CEntity__UpdateRwFrame)(vehicle);
        reinterpret_cast<WorldAddFn>(gPatterns.CWorld__Add)(vehicle);
    }

    // HORIZONTAL POSITION ONLY, LEAVING Z AND THE WHOLE BASIS ALONE.
    //
    // For a grounded ridden bike, Z and the pitch/roll of the basis are
    // suspension output - GTA resolved them against the road this frame and
    // they are more correct than a ~100ms-old snapshot from another machine.
    // This corrects where the bike is on the map without touching how it is
    // sitting on its wheels.
    void SetBikeHorizontalPosition(void *handle, float x, float y) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || !vehicle->m_matrix) {
            return;
        }

        const float z = vehicle->m_matrix->pos.z;

        reinterpret_cast<WorldRemoveFn>(gPatterns.CWorld__Remove)(vehicle);
        reinterpret_cast<SetPosnFn>(gPatterns.CPlaceable__SetPosn)(vehicle, x, y, z);
        reinterpret_cast<UpdateRwMatrixFn>(gPatterns.CEntity__UpdateRwMatrix)(vehicle);
        reinterpret_cast<UpdateRwFrameFn>(gPatterns.CEntity__UpdateRwFrame)(vehicle);
        reinterpret_cast<WorldAddFn>(gPatterns.CWorld__Add)(vehicle);
    }

    // HEADING ONLY, PRESERVING GTA'S PITCH AND ROLL.
    //
    // Yaw is navigation - where the rider pointed the bike - and is
    // legitimately network-authoritative. Pitch and roll are suspension and
    // lean, which this machine's physics resolved for itself. Rebuilding the
    // basis around the current Up vector keeps those intact while turning the
    // bike to the heading the owner reports.
    void SetBikeHeading(void *handle, const glm::quat &rotation) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || !vehicle->m_matrix) {
            return;
        }

        auto &matrix = *vehicle->m_matrix;

        const glm::vec3 up(matrix.at.x, matrix.at.y, matrix.at.z);
        const glm::vec3 wantedForward = glm::mat3_cast(glm::normalize(rotation))[1];

        // Project the wanted heading onto the plane GTA's own Up defines, so
        // only the yaw component of the packet is taken.
        glm::vec3 forward = wantedForward - up * glm::dot(wantedForward, up);

        if (glm::length(forward) < 1e-4f) {
            return;
        }

        forward = glm::normalize(forward);

        const glm::vec3 right = glm::normalize(glm::cross(forward, up));

        matrix.right = CVector(right.x, right.y, right.z);
        matrix.up    = CVector(forward.x, forward.y, forward.z);
        // matrix.at (GTA's up) deliberately untouched.

        reinterpret_cast<UpdateRwMatrixFn>(gPatterns.CEntity__UpdateRwMatrix)(vehicle);
        reinterpret_cast<UpdateRwFrameFn>(gPatterns.CEntity__UpdateRwFrame)(vehicle);
    }

    // ROLL, EASED TOWARD THE OWNER'S, KEEPING GTA'S PITCH AND HEADING.
    //
    // For a bike ON BOTH WHEELS, roll is suspension output and the network
    // must not touch it - that was the whole finding behind the current
    // policy. But during a WHEELIE the front wheel leaves the ground and
    // GTA's own roll stabilisation (m_fWheelieStabMult, tBikeHandlingData)
    // lives inside CBike::ProcessControl - which never runs for a remote
    // bike. With nothing stabilising it and nothing correcting it, the
    // remote copy is free to roll away and lie over sideways while the owner
    // is balanced on the rear wheel.
    //
    // So when contact is reduced, roll becomes network-authoritative again,
    // eased rather than snapped so it does not fight what little suspension
    // response remains. Pitch stays GTA's - that is the wheelie angle
    // itself, and it is legitimately different on each machine.
    void BlendBikeRoll(void *handle, const glm::quat &rotation, float alpha) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || !vehicle->m_matrix) {
            return;
        }

        auto &matrix = *vehicle->m_matrix;

        const glm::vec3 gtaForward(matrix.up.x, matrix.up.y, matrix.up.z);
        const glm::vec3 gtaUp(matrix.at.x, matrix.at.y, matrix.at.z);

        const glm::mat3 wire      = glm::mat3_cast(glm::normalize(rotation));
        const glm::vec3 wantedUp  = wire[2];

        // Only the component of the owner's Up that is perpendicular to our
        // own forward axis is roll; the rest is pitch, which stays ours.
        glm::vec3 targetUp = wantedUp - gtaForward * glm::dot(wantedUp, gtaForward);

        if (glm::length(targetUp) < 1e-4f) {
            return;
        }

        targetUp = glm::normalize(targetUp);

        glm::vec3 blended = glm::normalize(glm::mix(gtaUp, targetUp, glm::clamp(alpha, 0.0f, 1.0f)));

        // Rebuild an orthonormal basis around our forward and the blended up.
        glm::vec3 right = glm::cross(gtaForward, blended);

        if (glm::length(right) < 1e-4f) {
            return;
        }

        right = glm::normalize(right);

        const glm::vec3 up = glm::normalize(glm::cross(right, gtaForward));

        matrix.right = CVector(right.x, right.y, right.z);
        matrix.up    = CVector(gtaForward.x, gtaForward.y, gtaForward.z);
        matrix.at    = CVector(up.x, up.y, up.z);

        reinterpret_cast<UpdateRwMatrixFn>(gPatterns.CEntity__UpdateRwMatrix)(vehicle);
        reinterpret_cast<UpdateRwFrameFn>(gPatterns.CEntity__UpdateRwFrame)(vehicle);
    }

    // THE BIKE'S CURRENT ORIENTATION, AS A QUATERNION.
    //
    // The read half of the wheelie path. The interpolator needs a "current"
    // to aim FROM when a packet lands, and it has to be the live matrix
    // rather than anything cached, because GTA has integrated the chassis
    // since the last packet.
    glm::quat BikeOrientation(const void *handle) {
        auto *vehicle = AsVehicle(const_cast<void *>(handle));

        if (!vehicle || !vehicle->m_matrix) {
            return glm::identity<glm::quat>();
        }

        return QuatFromMatrix(*vehicle->m_matrix);
    }

    // WRITES AN ORIENTATION THE CALLER HAS ALREADY DECIDED ON.
    //
    // No blending of its own - deliberately. The wheelie path drives this
    // from Framework's Interpolator, which consumes a fixed error across the
    // packet window and stops when it is spent. Easing again in here would
    // put a second, unsynchronised filter in series with that one and the
    // pair would never converge.
    void SetBikeOrientation(void *handle, const glm::quat &rotation) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || !vehicle->m_matrix) {
            return;
        }

        ApplyQuatToMatrix(*vehicle->m_matrix, glm::normalize(rotation));

        reinterpret_cast<UpdateRwMatrixFn>(gPatterns.CEntity__UpdateRwMatrix)(vehicle);
        reinterpret_cast<UpdateRwFrameFn>(gPatterns.CEntity__UpdateRwFrame)(vehicle);
    }

    // EASES THE WHOLE BASIS TOWARD THE OWNER'S, KEEPING POSITION.
    //
    // For a GROUNDED bike this would be wrong - pitch and roll are that
    // machine's own suspension output and forcing them is the chassis hop.
    // During a WHEELIE it is the only correct thing: the remote bike is not
    // doing the wheelie at all (measured forward.z ~0.1 against the owner's
    // ~0.99, an 80-degree pitch disagreement), because the wheelie is
    // produced by CBike::ProcessControl, which never runs for a remote bike.
    //
    // Eased rather than snapped so the entry and exit do not step.
    void BlendBikeOrientation(void *handle, const glm::quat &rotation, float alpha) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || !vehicle->m_matrix) {
            return;
        }

        auto &matrix = *vehicle->m_matrix;

        const glm::quat current = QuatFromMatrix(matrix);
        const glm::quat blended = glm::slerp(current, glm::normalize(rotation), glm::clamp(alpha, 0.0f, 1.0f));

        ApplyQuatToMatrix(matrix, blended);

        reinterpret_cast<UpdateRwMatrixFn>(gPatterns.CEntity__UpdateRwMatrix)(vehicle);
        reinterpret_cast<UpdateRwFrameFn>(gPatterns.CEntity__UpdateRwFrame)(vehicle);
    }

    void SetVehiclePose(void *handle, float x, float y, float z, const glm::quat &rotation) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle) {
            return;
        }

        if (auto *matrix = MatrixOf(vehicle)) {
            // OUT OF THE WORLD BEFORE THE WRITE, BACK IN AFTER.
            //
            // GTA indexes every entity into the world SECTOR LISTS by the
            // position it had when it was added, and CPhysical walks those
            // lists to decide what a moving entity can collide with. Writing
            // a new matrix in place moves the bike but leaves it registered
            // in the sector it used to be in, so its collision is tested
            // against the wrong neighbourhood - including, crucially, the
            // wrong piece of road surface.
            //
            // That is why this hits a BIKE hardest. A car rests on four
            // suspension lines and CAutomobile re-derives them every frame;
            // it can survive a frame of bad neighbours. A bike is balanced on
            // two contact points, and CBike has no roll stability of its own
            // beyond those points - lose the ground under them and the only
            // thing left acting on it is gravity plus whatever turn speed it
            // carries, so it falls over and keeps falling, which is exactly
            // the "lying on its side while the owner rides upright" picture.
            //
            // MTA does this in CEntitySA::SetMatrix and never writes a matrix
            // without it: CWorld::Remove -> write -> UpdateRwMatrix ->
            // UpdateRwFrame -> CWorld::Add. CWorld::Add re-registers the
            // entity in the sector for its NEW position and puts a non-static
            // physical back on the moving list; CWorld::Remove takes it off
            // both. We already resolve both patterns.
            reinterpret_cast<WorldRemoveFn>(gPatterns.CWorld__Remove)(vehicle);

            ApplyQuatToMatrix(*matrix, rotation);

            reinterpret_cast<SetPosnFn>(gPatterns.CPlaceable__SetPosn)(vehicle, x, y, z);

            // THE MESH IS NOT DRAWN FROM THE MATRIX WE JUST WROTE.
            //
            // CMatrix::right/up/at is the physics matrix. The renderer draws
            // from the attached RwMatrix, and the frame hierarchy hangs off
            // that - so a pose write that stops here moves the collision
            // while the visible model follows whatever RenderWare still
            // holds. MTA never writes a matrix without these two calls; they
            // are the tail of CEntitySA::SetMatrix.
            //
            // A car mostly hides it: CAutomobile::ProcessControl rebuilds the
            // RW matrix from its own suspension work every frame, so the
            // stale frame is refreshed for us. A BIKE does not. CBike caches
            // its render transform in m_mLeanMatrix behind
            // m_bLeanMatrixCalculated, rebuilt from the entity matrix plus
            // GTA's own lean - so a matrix we wrote but never published is
            // read back a frame later, half-applied, and the bike renders
            // crooked and drifts as the error accumulates.
            reinterpret_cast<UpdateRwMatrixFn>(gPatterns.CEntity__UpdateRwMatrix)(vehicle);
            reinterpret_cast<UpdateRwFrameFn>(gPatterns.CEntity__UpdateRwFrame)(vehicle);

            // Back into the sector lists for where it now is. CWorld::Add
            // calls UpdateRwMatrix/UpdateRwFrame itself, but they are left
            // above so the ordering is explicit and matches MTA.
            reinterpret_cast<WorldAddFn>(gPatterns.CWorld__Add)(vehicle);

            return;
        }

        reinterpret_cast<SetPosnFn>(gPatterns.CPlaceable__SetPosn)(vehicle, x, y, z);
    }

    // GTA DECIDES HOW MUCH OF A VEHICLE TO SIMULATE FROM ITS STATUS, AND WE
    // NEVER SET IT.
    //
    // The bike-specific consequences, from gta-reversed:
    //
    //  - CQuadBike::ProcessAI (0x6CE460, reversed) opens with
    //      if (GetStatus() != STATUS_PLAYER)
    //          return CAutomobile::ProcessAI(...);
    //    so a bike that is not STATUS_PLAYER never reaches the
    //    `m_pDriver && m_pDriver->IsPlayer()` branch that calls
    //    ProcessControlInputs - which is the ONLY thing that clears
    //    vehicleFlags.bIsHandbrakeOn (CVehicle's constructor sets it true,
    //    Vehicle.cpp:253) and the only thing that applies steer and lean.
    //    CBike::ProcessControl (0x6B9250) is not reversed, but it is the same
    //    shape and consumes the same fields.
    //
    //  - CBike::DamageKnockOffRider (Bike.cpp:181) takes a different, much
    //    weaker resistance path when status is not STATUS_PLAYER, so a remote
    //    rider comes off the bike far too easily.
    //
    //  - CPhysical::ProcessCollision returns early for STATUS_SIMPLE after
    //    SetIsInSafePosition(true), and CWorld::Process only calls
    //    ProcessCollision when !GetIsInSafePosition() - so a STATUS_SIMPLE
    //    vehicle skips collision, suspension and shift entirely.
    //
    // A car mostly survives the wrong status: its roll is pinned by four
    // suspension points and CAutomobile rebuilds them every frame. A bike
    // balances on two contact points and has no roll stability beyond them.
    //
    // GTA's own rule, from CAutomobile::BreakTowLink (Automobile.cpp:3369):
    // a driver that IsPlayer() means STATUS_PLAYER, any other driver means
    // STATUS_PHYSICS, and no driver means STATUS_ABANDONED. MTA manages
    // status explicitly for the same reason (CClientVehicle/CVehicleSA);
    // we were leaving whatever CreateCarForScript happened to produce.
    void SyncVehicleSimulationStatus(void *handle) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle) {
            return;
        }

        // A wreck has its own status and must keep it, or the game stops
        // treating it as a wreck.
        if (vehicle->m_nStatus == STATUS_WRECKED) {
            return;
        }

        const auto *driver = vehicle->m_pDriver;

        const bool driverIsPlayer =
            driver && (driver->m_nPedType == PED_TYPE_PLAYER1 || driver->m_nPedType == PED_TYPE_PLAYER2);

        const eEntityStatus wanted = driverIsPlayer  ? STATUS_PLAYER
                                     : driver        ? STATUS_PHYSICS
                                                     : STATUS_ABANDONED;

        if (vehicle->m_nStatus != wanted) {
            vehicle->m_nStatus = wanted;
        }
    }

    void SetVehicleInterior(void *handle, std::uint8_t interior) {
        if (auto *vehicle = AsVehicle(handle)) {
            vehicle->m_nAreaCode = interior;
        }
    }

    std::uint8_t VehicleInterior(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        return vehicle ? vehicle->m_nAreaCode : 0;
    }

    void SetVehicleColors(void *handle, std::uint8_t primary, std::uint8_t secondary) {
        if (auto *vehicle = AsVehicle(handle)) {
            vehicle->m_nPrimaryColor   = primary;
            vehicle->m_nSecondaryColor = secondary;
        }
    }

    void SetVehicleRgbColors(void *handle, const std::uint32_t *colours, int count) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || !colours || count <= 0) {
            return;
        }

        VehicleRgb rgb {};

        for (int i = 0; i < Shared::kVehicleColorCount; ++i) {
            const auto packed = Shared::TweakVehicleRgb(colours[i < count ? i : 0]);

            rgb.r[i] = Shared::RgbR(packed);
            rgb.g[i] = Shared::RgbG(packed);
            rgb.b[i] = Shared::RgbB(packed);
        }

        gVehicleRgb[vehicle] = rgb;

        vehicle->m_nPrimaryColor    = 0;
        vehicle->m_nSecondaryColor  = 1;
        vehicle->m_nTertiaryColor   = 2;
        vehicle->m_nQuaternaryColor = 3;
    }

    void ClearVehicleRgbColors(void *handle) {
        if (handle) {
            gVehicleRgb.erase(handle);
        }
    }

    void ApplyVehicleCosmetics(void *handle, std::uint8_t paintjob, const std::uint16_t *upgrades, int upgradeSlots) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle) {
            return;
        }

        using SetRemapFn  = void(__thiscall *)(CVehicle *, int);
        using AddUpgradeFn = void(__thiscall *)(CVehicle *, int);

        reinterpret_cast<SetRemapFn>(gPatterns.CVehicle__SetRemap)(vehicle, static_cast<int>(paintjob));

        if (!upgrades || upgradeSlots <= 0) {
            return;
        }

        for (int i = 0; i < upgradeSlots; ++i) {
            const auto part = upgrades[i];

            if (part == 0) {
                continue;
            }

            LoadModelNow(static_cast<int>(part));
            reinterpret_cast<AddUpgradeFn>(gPatterns.CVehicle__AddVehicleUpgrade)(vehicle, static_cast<int>(part));
        }
    }

    void ApplyVehicleScriptState(void *handle, bool engineOn, bool sirenOn, std::uint8_t overrideLights, bool locked) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle) {
            return;
        }

        vehicle->bEngineOn      = engineOn;
        vehicle->bSirenOrAlarm  = sirenOn;
        vehicle->m_nOverrideLights = overrideLights <= 2 ? overrideLights : 0;
        vehicle->m_eDoorLock    = locked ? DOORLOCK_LOCKED : DOORLOCK_UNLOCKED;
    }

    void ApplyVehicleExtras(void *handle, std::uint8_t dirtLevel, bool taxiLight, std::uint32_t headlightColor, bool frozen, bool damageProof,
                            bool collisions, bool landingGearDown, bool lightsOn, bool engineBroken, bool wheelsCanBurst, bool canBeDamaged,
                            std::uint8_t nitroCount) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle) {
            return;
        }

        SetVehicleHeadlightColor(handle, headlightColor);

        vehicle->m_fDirtLevel = static_cast<float>(dirtLevel > 15 ? 15 : dirtLevel);

        vehicle->bDontApplySpeed   = frozen;
        vehicle->bDisableMoveForce = frozen;
        vehicle->bDisableTurnForce = frozen;
        vehicle->bUsesCollision    = collisions;
        vehicle->bLightsOn         = lightsOn;
        vehicle->bEngineBroken     = engineBroken;
        vehicle->bTyresDontBurst   = !wheelsCanBurst;
        vehicle->bCanBeDamaged     = canBeDamaged;
        vehicle->m_nNitroBoosts    = nitroCount;

        if (frozen) {
            vehicle->m_vecMoveSpeed = CVector();
            vehicle->m_vecTurnSpeed = CVector();
        }

        vehicle->bBulletProof    = damageProof;
        vehicle->bFireProof      = damageProof;
        vehicle->bCollisionProof = damageProof;
        vehicle->bMeleeProof     = damageProof;
        vehicle->bExplosionProof = damageProof;

        if (auto *car = AsAutomobile(vehicle)) {
            car->m_nAutomobileFlags.bTaxiLight = taxiLight;
        }

        if (vehicle->m_nVehicleClass == VEHICLE_PLANE || vehicle->m_nVehicleClass == VEHICLE_FPLANE) {
            static_cast<CPlane *>(vehicle)->m_fLandingGearStatus = landingGearDown ? 0.0f : 1.0f;
        }
    }

    void SetVehiclePlateText(void *handle, const std::string &text) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || !gPatterns.CModelInfo__ms_modelInfoPtrs || !gPatterns.CVehicle__CustomCarPlate_TextureCreate) {
            return;
        }

        const auto modelIndex = vehicle->m_nModelIndex;
        if (modelIndex < 0 || modelIndex >= 20000) {
            return;
        }

        auto **modelInfos = reinterpret_cast<CBaseModelInfo **>(gPatterns.CModelInfo__ms_modelInfoPtrs);
        auto *model       = reinterpret_cast<CVehicleModelInfo *>(modelInfos[modelIndex]);

        if (!model || !model->m_pPlateMaterial) {
            return;
        }

        std::memset(model->m_szPlateText, 0, 8);
        const auto n = (std::min)(text.size(), std::size_t {8});
        if (n) {
            std::memcpy(model->m_szPlateText, text.data(), n);
        }

        if (gPatterns.CVehicle__CustomCarPlate_TextureDestroy && vehicle->m_pCustomCarPlate) {
            using DestroyPlateFn = void(__thiscall *)(CVehicle *);
            reinterpret_cast<DestroyPlateFn>(gPatterns.CVehicle__CustomCarPlate_TextureDestroy)(vehicle);
        }

        using CreatePlateFn = bool(__thiscall *)(CVehicle *, CVehicleModelInfo *);
        reinterpret_cast<CreatePlateFn>(gPatterns.CVehicle__CustomCarPlate_TextureCreate)(vehicle, model);
    }

    void SetVehicleAlpha(void *handle, std::uint8_t alpha) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || !vehicle->m_pRwObject) {
            return;
        }

        using SetAlphaFn = void(__cdecl *)(void *, int);

        reinterpret_cast<SetAlphaFn>(gPatterns.CVisibilityPlugins__SetClumpAlpha)(vehicle->m_pRwObject, static_cast<int>(alpha));
    }

    bool VehicleLandingGearDown(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        if (!vehicle) {
            return true;
        }

        if (vehicle->m_nVehicleClass != VEHICLE_PLANE && vehicle->m_nVehicleClass != VEHICLE_FPLANE) {
            return true;
        }

        return static_cast<const CPlane *>(vehicle)->m_fLandingGearStatus < 0.5f;
    }

    bool VehicleLightsOn(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        return vehicle && vehicle->bLightsOn;
    }

    bool VehicleOnGround(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        return vehicle && vehicle->bOnSolidSurface;
    }

    // CBike::m_nNumContactWheels, counted over FOUR suspension lines.
    //
    // A bike has two wheels but NUM_SUSP_LINES == 4 (Bike.h), so a normal
    // grounded bike reports 4 here, not 2. Reading this as "wheels on the
    // ground" and testing `< 2` is why an earlier wheelie gate never fired.
    int VehicleContactWheels(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        if (!vehicle || !IsBikeHandle(vehicle)) {
            return -1;
        }

        return static_cast<int>(static_cast<const CBike *>(vehicle)->m_nNumContactWheels);
    }

    // GTA'S OWN WHEELIE MEASURE: THE FORWARD VECTOR'S Z COMPONENT.
    //
    // From CQuadBike::ProcessControl (QuadBike.cpp:222), which is the
    // reversed analogue of the bike code:
    //
    //   if (AreFrontWheelsNotTouchingGround())
    //       if (... && m_matrix->GetForward().z > 0.0f)
    //           ... fabs(m_pHandling->m_fWheelieAng - m_matrix->GetForward().z) ...
    //   else if (... m_matrix->GetForward().z < 0.0f)
    //           ... fabs(m_pHandling->m_fStoppieAng - m_matrix->GetForward().z) ...
    //
    // So the game does NOT count wheels or convert to an angle. It reads
    // forward.z directly and compares it against m_fWheelieAng, which
    // cHandlingDataMgr stores as sin(angle) (cHandlingDataMgr.cpp:359,
    // `m_fWheelieAng = sin(DegreesToRadians(m_fWheelieAng))`) - the same
    // space as forward.z. Positive is nose-up, negative nose-down.
    //
    // Returns forward.z, so 0 is level, positive a wheelie, negative a
    // stoppie. Callers compare against BikeWheelieAngle() below.
    float BikeForwardZ(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        if (!vehicle || !vehicle->m_matrix) {
            return 0.0f;
        }

        // GTA's m_forward is plugin-sdk's `up`.
        return vehicle->m_matrix->up.z;
    }

    // m_fWheelieAng for this bike, in the same sin() space as forward.z.
    // The angle at which GTA considers the bike to be in a full wheelie.
    float BikeWheelieAngle(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        if (!vehicle || !IsBikeHandle(vehicle)) {
            return 0.0f;
        }

        const auto *bike = static_cast<const CBike *>(vehicle);

        if (!bike->m_pBikeHandlingData) {
            return 0.0f;
        }

        return bike->m_pBikeHandlingData->m_fWheelieAng;
    }

    // IS THE BIKE OFF ITS SUSPENSION - WHEELIE, STOPPIE OR AIRBORNE?
    //
    // gta-reversed spells out the test (Bike.h):
    //
    //   IsAnyWheelNotMakingContactWithGround()
    //       any_of(m_aWheelRatios, [](r) { return r == 1.0f; })
    //
    // A ratio of exactly 1.0 is a fully extended suspension line, i.e. that
    // line found no ground. This is the honest signal; the contact COUNT is
    // over four lines and stays at 4 far longer than the bike is level.
    //
    // Returns how many of the four lines are off the ground: 0 grounded,
    // 4 fully airborne, in between a wheelie or stoppie.
    int BikeWheelsOffGround(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        if (!vehicle || !IsBikeHandle(vehicle)) {
            return -1;
        }

        const auto *bike = static_cast<const CBike *>(vehicle);

        int off = 0;

        for (const float ratio : bike->m_aWheelRatios) {
            if (ratio >= 1.0f) {
                ++off;
            }
        }

        return off;
    }

    float VehicleSteerAngle(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        return vehicle ? vehicle->m_fSteerAngle : 0.0f;
    }

    bool VehicleHandbrakeOn(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        return vehicle && vehicle->bIsHandbrakeOn;
    }

    bool IsBikeHandle(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        return vehicle && (vehicle->m_nVehicleSubClass == VEHICLE_BIKE || vehicle->m_nVehicleSubClass == VEHICLE_BMX);
    }

    bool IsLocalPlayerDriving(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        return vehicle && vehicle->m_pDriver == Internal::LocalPed();
    }

    bool VehicleHasDriver(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        return vehicle && vehicle->m_pDriver != nullptr;
    }

    void BindRemoteVehicleProcessControl(void *handle) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle) {
            return;
        }

        void **vtable = *reinterpret_cast<void ***>(vehicle);

        if (gOriginalProcessControl.contains(vtable)) {
            return;
        }

        void **controlSlot = &vtable[kProcessControlSlot];
        void **blowSlot    = &vtable[kBlowUpCarSlot];

        DWORD previous = 0;

        if (VirtualProtect(controlSlot, sizeof(void *), PAGE_EXECUTE_READWRITE, &previous) == 0) {
            return;
        }

        gOriginalProcessControl.emplace(vtable, reinterpret_cast<ProcessControlFn>(*controlSlot));
        *controlSlot = reinterpret_cast<void *>(&VehicleProcessControl_Hook);

        DWORD ignored = 0;
        VirtualProtect(controlSlot, sizeof(void *), previous, &ignored);

        if (gOriginalBlowUpCar.contains(vtable)) {
            return;
        }

        if (VirtualProtect(blowSlot, sizeof(void *), PAGE_EXECUTE_READWRITE, &previous) == 0) {
            return;
        }

        gOriginalBlowUpCar.emplace(vtable, reinterpret_cast<BlowUpCarFn>(*blowSlot));
        *blowSlot = reinterpret_cast<void *>(&BlowUpCar_Hook);

        VirtualProtect(blowSlot, sizeof(void *), previous, &ignored);
    }

    void InstallVehicleColorHooks() {
        if (gOriginalSetupRender) {
            return;
        }

        MH_CreateHook(reinterpret_cast<void *>(gPatterns.CVehicle__SetupRender), reinterpret_cast<void *>(&SetupRender_Hook),
                      reinterpret_cast<void **>(&gOriginalSetupRender));
        MH_EnableHook(reinterpret_cast<void *>(gPatterns.CVehicle__SetupRender));
    }

    void InstallVehicleExplosionHooks() {
        if (gOriginalKillPedsIn) {
            return;
        }

        MH_CreateHook(reinterpret_cast<void *>(gPatterns.CVehicle__KillPedsInVehicle), reinterpret_cast<void *>(&KillPedsInVehicle_Hook),
                      reinterpret_cast<void **>(&gOriginalKillPedsIn));
        MH_EnableHook(reinterpret_cast<void *>(gPatterns.CVehicle__KillPedsInVehicle));

        MH_CreateHook(reinterpret_cast<void *>(gPatterns.CVehicle__KillPedsGettingInVehicle), reinterpret_cast<void *>(&KillPedsGettingInVehicle_Hook),
                      reinterpret_cast<void **>(&gOriginalKillPedsGettingIn));
        MH_EnableHook(reinterpret_cast<void *>(gPatterns.CVehicle__KillPedsGettingInVehicle));
    }
} // namespace VantaMP::SDK
