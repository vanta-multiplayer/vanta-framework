#include "vehicle.h"

#include "ped/access.h"
#include "sdk/game_sdk.h"
#include "sdk/patterns.h"

#include <utils/safe_win32.h>

#include <CAutomobile.h>
#include <CDamageManager.h>
#include <CDoor.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>

namespace VantaMP::SDK {
    namespace {
        using CreateCarFn      = CVehicle *(__cdecl *)(int, CVector, unsigned char);
        using SetPosnFn        = void(__thiscall *)(void *, float, float, float);
        using SetOrientationFn = void(__thiscall *)(void *, float, float, float);
        using WorldRemoveFn    = void(__cdecl *)(void *);
        using SetupDamageFn    = void(__thiscall *)(CVehicle *);

        // plugin-sdk's CVehicle::SetupDamageAfterLoad is vtable slot 51.
        // The status bytes alone do not detach a door; this rebuilds the model.
        constexpr std::size_t kSetupDamageAfterLoadSlot = 51;

        // CVehicle::OpenDoor. MTA's 3-arg wrapper hits this with the node
        // table below; writing CDoor::m_fAngle alone does not move the mesh.
        constexpr std::size_t kOpenDoorSlot = 27;

        constexpr int kDoorNodeIndex[6] = {0x10, 0x11, 0x0A, 0x08, 0x0B, 0x09};
        constexpr int kDoorCount        = 6;

        using OpenDoorFn = void(__thiscall *)(CVehicle *, CPed *, int, int, float, bool);

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
        glm::quat QuatFromMatrix(const CMatrix &matrix) {
            const glm::mat3 axes(glm::vec3(matrix.right.x, matrix.right.y, matrix.right.z), glm::vec3(matrix.up.x, matrix.up.y, matrix.up.z), glm::vec3(matrix.at.x, matrix.at.y, matrix.at.z));

            return glm::normalize(glm::quat_cast(axes));
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

        constexpr unsigned char kDoorMissing = 4;

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

                if (damage && damage->m_anDoorsStatus[i] >= kDoorMissing) {
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

        std::int8_t SteerToWire(float angle) {
            // Negated: stick-right is a negative steer angle in GTA.
            const float scaled = std::clamp(-angle / kSteerScale, -128.0f, 127.0f);

            return static_cast<std::int8_t>(scaled);
        }

        std::uint8_t PedalToWire(float pedal) {
            return static_cast<std::uint8_t>(std::clamp(pedal / kPedalScale, 0.0f, 255.0f));
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

        using ProcessControlFn = void(__thiscall *)(void *);

        std::unordered_map<void **, ProcessControlFn> gOriginalProcessControl;

        int SlotOfRemoteDriver(CPed *driver) {
            if (!driver || driver == Internal::LocalPed()) {
                return -1;
            }

            for (int slot = 2; slot < Game::kMaxPlayers; ++slot) {
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

            void *localPad  = nullptr;
            void *remotePad = nullptr;
            std::array<std::uint8_t, Game::kPadSize> saved {};

            if (slot >= 0) {
                localPad  = reinterpret_cast<Internal::GetPadFn>(gPatterns.CPad__GetPad)(0);
                remotePad = Game::PadAt(slot);

                if (localPad && remotePad) {
                    std::memcpy(saved.data(), localPad, Game::kPadSize);
                    std::memcpy(localPad, remotePad, Game::kPadSize);
                }
            }

            found->second(vehicle);

            if (localPad && remotePad) {
                std::memcpy(remotePad, localPad, Game::kPadSize);
                std::memcpy(localPad, saved.data(), Game::kPadSize);
            }
        }
    } // namespace

    bool IsVehicleModel(int modelId) {
        return modelId >= kFirstVehicleModel && modelId <= kLastVehicleModel;
    }

    void *CreateScriptVehicle(int modelId, float x, float y, float z, float heading, std::uint8_t primaryColour, std::uint8_t secondaryColour) {
        if (!IsVehicleModel(modelId)) {
            return nullptr;
        }

        auto *vehicle = reinterpret_cast<CreateCarFn>(gPatterns.CCarCtrl__CreateCarForScript)(modelId, CVector(x, y, z), 0);

        if (!vehicle) {
            return nullptr;
        }

        reinterpret_cast<SetOrientationFn>(gPatterns.CPlaceable__SetOrientation)(vehicle, 0.0f, 0.0f, heading);

        vehicle->m_nPrimaryColor   = primaryColour;
        vehicle->m_nSecondaryColor = secondaryColour;

        // CreateCarForScript locks the doors: it is written for mission props.
        // vehicleFlags.bIsLocked is a deletion guard and is left alone.
        vehicle->m_eDoorLock = DOORLOCK_UNLOCKED;

        return vehicle;
    }

    void DestroyScriptVehicle(void *handle) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle) {
            return;
        }

        reinterpret_cast<WorldRemoveFn>(gPatterns.CWorld__Remove)(vehicle);
        DeleteFromVehiclePool(vehicle);
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

    void ApplyVehicleDamage(void *handle, std::uint8_t engine, std::uint64_t doorsPacked, std::uint32_t wheelsPacked, std::uint32_t panels, std::uint32_t lights,
                            bool rebuildModel) {
        auto *vehicle = AsVehicle(handle);
        auto *damage  = DamageManagerOf(vehicle);

        if (!damage) {
            return;
        }

        damage->m_nEngineStatus = engine;
        UnpackDoors(doorsPacked, damage->m_anDoorsStatus);
        UnpackWheels(wheelsPacked, damage->m_anWheelsStatus);
        damage->m_nPanelsStatus = panels;
        damage->m_nLightsStatus = lights;

        // GTA starts the blow-up sequence around 250. Rebuilding the model
        // then, or while a ped is hitting the tank, pops components under
        // them and the physics throws them like a ragdoll they cannot cancel.
        if (!rebuildModel || vehicle->m_fHealth <= 250.0f) {
            return;
        }

        auto *vtable = *reinterpret_cast<SetupDamageFn **>(vehicle);

        vtable[kSetupDamageAfterLoadSlot](vehicle);
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

    void ApplyVehicleControls(void *handle, std::int8_t steer, std::uint8_t accelerator, std::uint8_t brake) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || IsLocalPlayerDriving(vehicle)) {
            return;
        }

        vehicle->m_fSteerAngle = -static_cast<float>(steer) * kSteerScale;
        vehicle->m_fGasPedal   = static_cast<float>(accelerator) * kPedalScale;
        vehicle->m_fBreakPedal = static_cast<float>(brake) * kPedalScale;
    }

    void SetVehiclePose(void *handle, float x, float y, float z, const glm::quat &rotation) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle) {
            return;
        }

        if (auto *matrix = MatrixOf(vehicle)) {
            ApplyQuatToMatrix(*matrix, rotation);
        }

        reinterpret_cast<SetPosnFn>(gPatterns.CPlaceable__SetPosn)(vehicle, x, y, z);
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

        void **slot = &vtable[kProcessControlSlot];

        DWORD previous = 0;

        if (VirtualProtect(slot, sizeof(void *), PAGE_EXECUTE_READWRITE, &previous) == 0) {
            return;
        }

        gOriginalProcessControl.emplace(vtable, reinterpret_cast<ProcessControlFn>(*slot));
        *slot = reinterpret_cast<void *>(&VehicleProcessControl_Hook);

        DWORD ignored = 0;
        VirtualProtect(slot, sizeof(void *), previous, &ignored);
    }
} // namespace VantaMP::SDK
