#include "game/sync/vehicle_sync.h"

#include "game/sync/player_sync.h"
#include "sdk/game.h"
#include "sdk/input/input.h"
#include "sdk/world/streaming.h"
#include "shared/entity/player_entity.h"
#include "shared/world/world_space.h"
#include "shared/rpc/player_enter_vehicle.h"
#include "shared/rpc/player_exit_vehicle.h"
#include "shared/rpc/player_occupancy_abort.h"
#include "shared/rpc/vehicle_damage_report.h"
#include "shared/sync_rates.h"
#include "shared/vehicle/colors.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/vehicle/upgrades.h"

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <utils/interpolator.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace VantaMP::Game {
    namespace {
        namespace Replication = Framework::Networking::Replication;

        struct RemoteVehicle {
            void                         *handle = nullptr;
            Framework::Utils::Interpolator interpolator;
            glm::vec3                      aimedPosition {};
            glm::quat                      aimedRotation = glm::identity<glm::quat>();
            float                          speed        = 0.0f;
            std::uint32_t                  whenMs       = 0;
            bool                           aimed        = false;

            glm::vec3 velocity {};
            glm::vec3 angular {};
            float     health   = 1000.0f;
            bool      engineOn = true;
            bool      sirenOn  = false;

            std::int8_t  steer       = 0;
            std::uint8_t accelerator = 0;
            std::uint8_t brake       = 0;

            std::uint8_t  lastEngine  = 0;
            std::uint64_t lastDoors   = 0;
            std::uint32_t lastWheels  = 0;
            std::uint32_t lastPanels  = 0;
            std::uint32_t lastLights  = 0;
            std::uint64_t lastRatios       = 0;
            std::uint64_t lastEntityRatios = 0;
            std::uint64_t heldRatios       = 0;
            float         doorStart[6]     {};
            float         doorTarget[6]    {};
            std::uint32_t doorInterpMs     = 0;
            bool          doorInterp       = false;
            bool          haveDamage  = false;
            bool          wrecked     = false;
            std::uint32_t lastDamageReportMs = 0;
            std::uint32_t lastRatioReportMs  = 0;
            std::uint64_t lastSentRatios     = 0;

            std::uint8_t  lastPaintjob  = 255;
            std::array<std::uint32_t, Shared::kVehicleColorCount> lastColours {};
            bool haveColours = false;
            std::array<std::uint16_t, Shared::kUpgradeSlots> lastUpgrades {};

            bool         haveScriptState     = false;
            bool         lastEngineOn        = true;
            bool         lastSirenOn         = false;
            std::uint8_t lastOverrideLights  = 0;
            bool         lastLocked          = false;
            std::uint8_t lastDirt            = 255;
            bool         lastTaxiLight       = false;
            std::uint32_t lastHeadlight      = 0;
            bool         lastFrozen          = false;
            bool         lastDamageProof     = false;
            bool         lastCollisions      = true;
            bool         lastLandingGear     = true;
            bool         lastLightsOn        = false;
            bool         lastEngineBroken    = false;
            bool         lastWheelsCanBurst  = true;
            bool         lastCanBeDamaged    = true;
            std::uint8_t lastNitroCount      = 0;
            std::string  lastPlateText;
            std::uint8_t lastVariant         = 255;
            std::uint8_t lastVariant2        = 255;
            bool         haveExtras          = false;
            bool         haveVariants        = false;
        };

        std::unordered_map<std::uint64_t, RemoteVehicle> gVehicles;

        // MTA's stream-in warp: a ped that appeared already in a seat,
        // remembered until the car exists and they are put in it.
        std::unordered_map<int, bool> gLateJoinWarp;

        void RememberLateJoin(int slot) {
            gLateJoinWarp[slot] = true;
        }

        void ForgetLateJoin(int slot) {
            gLateJoinWarp.erase(slot);
        }

        [[nodiscard]] bool ConsumeLateJoin(int slot) {
            const auto found = gLateJoinWarp.find(slot);

            return found != gLateJoinWarp.end() && found->second;
        }

        enum class LocalRide { OnFoot, Entering, Seated, Leaving };

        LocalRide gLocalRide = LocalRide::OnFoot;

        // First frame of a walk-in the enter task still has door 0, which is
        // also FRONT_LEFT. Waiting one tick lets CreateFirstSubTask write the
        // door the owner actually walked to, so remotes shuffle when that
        // door is the passenger side.
        bool gAwaitEnterDoor = false;

        // Stick held when the climb started must not cancel it — that is
        // just walking up to the door. A new press, a jump, or a release
        // then press is the player changing their mind.
        bool gEnterStickHeld         = false;
        bool gEnterJumpHeld          = false;
        bool gBlockEnterUntilRelease = false;
        bool gBlockExitUntilRelease  = false;

        constexpr float kAbortStick = 48.0f;

        [[nodiscard]] float StickMagnitude(const SDK::ControllerInput &input) {
            const auto x = static_cast<float>(input.leftStickX);
            const auto y = static_cast<float>(input.leftStickY);

            return std::sqrt(x * x + y * y);
        }

        void RememberEnterInput() {
            const auto input = SDK::ReadLocalInput();

            gEnterStickHeld = StickMagnitude(input) > kAbortStick;
            gEnterJumpHeld  = (input.buttons & SDK::Button::Jump) != 0;
        }

        [[nodiscard]] bool LocalWantsToAbortEnter() {
            const auto input    = SDK::ReadLocalInput();
            const bool jump     = (input.buttons & SDK::Button::Jump) != 0;
            const bool jumpEdge = jump && !gEnterJumpHeld;

            gEnterJumpHeld = jump;

            if (jumpEdge) {
                return true;
            }

            if (StickMagnitude(input) > kAbortStick) {
                return !gEnterStickHeld;
            }

            gEnterStickHeld = false;
            return false;
        }

        [[nodiscard]] bool OccupancyInputIdle() {
            const auto input = SDK::ReadLocalInput();

            return (input.buttons & SDK::Button::EnterExit) == 0 && (input.buttons & SDK::Button::Jump) == 0 && StickMagnitude(input) <= kAbortStick;
        }

        void PulseOccupancyBlocks(const SDK::PedOccupancy &occupancy) {
            if (gBlockEnterUntilRelease) {
                if (occupancy.entering) {
                    SDK::AbortVehicleEnter(0);
                }

                if (OccupancyInputIdle()) {
                    gBlockEnterUntilRelease = false;
                }
            }

            if (gBlockExitUntilRelease) {
                if (occupancy.leaving) {
                    SDK::AbortVehicleExit(0, occupancy.vehicle, occupancy.seat);
                }

                if (OccupancyInputIdle()) {
                    gBlockExitUntilRelease = false;
                }
            }
        }

        void *HandleOf(std::uint64_t vehicleId) {
            const auto found = gVehicles.find(vehicleId);

            return found != gVehicles.end() ? found->second.handle : nullptr;
        }

        void SendLocalEnter(std::uint64_t vehicleId, std::uint8_t seat, bool warp, std::uint8_t door) {
            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            Shared::RPC::PlayerEnterVehicle enter;
            enter.FromParameters(0, vehicleId, seat, warp, door);
            net->BroadcastRPC(enter);

            Framework::Logging::GetLogger("Vehicle")->debug("local enter vehicle {} seat {} door {} warp {}", vehicleId, seat, door, warp);
        }

        void SendLocalExit(bool warp, std::uint8_t door, bool abortEnter = false) {
            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            Shared::RPC::PlayerExitVehicle exit;
            exit.FromParameters(0, warp, door, abortEnter);
            net->BroadcastRPC(exit);

            Framework::Logging::GetLogger("Vehicle")->debug("local exit door {} warp {} abortEnter {}", door, warp, abortEnter);
        }

        void WatchLocalOccupancy() {
            const auto occupancy = SDK::QueryPedOccupancy(0);
            const auto vehicleId = VehicleNetworkIdOf(occupancy.vehicle);

            PulseOccupancyBlocks(occupancy);

            switch (gLocalRide) {
            case LocalRide::OnFoot:
                if (gBlockEnterUntilRelease) {
                    gAwaitEnterDoor = false;
                    break;
                }

                if (occupancy.seated && vehicleId != 0) {
                    gAwaitEnterDoor = false;
                    SendLocalEnter(vehicleId, occupancy.seat, true, occupancy.door);
                    gLocalRide = LocalRide::Seated;
                }
                else if (occupancy.entering && vehicleId != 0) {
                    if (!gAwaitEnterDoor) {
                        gAwaitEnterDoor = true;
                        break;
                    }

                    gAwaitEnterDoor = false;
                    RememberEnterInput();
                    SendLocalEnter(vehicleId, occupancy.seat, false, occupancy.door);
                    gLocalRide = LocalRide::Entering;
                }
                else {
                    gAwaitEnterDoor = false;
                }
                break;

            case LocalRide::Entering:
                gAwaitEnterDoor = false;

                if (occupancy.seated) {
                    // JACK COMPLETES HERE. The walk packet left the
                    // occupant in the seat; this seated packet is what
                    // lets the server throw them out and give us the car.
                    if (vehicleId != 0) {
                        SendLocalEnter(vehicleId, occupancy.seat, true, occupancy.door);
                    }

                    gLocalRide = LocalRide::Seated;
                }
                else if (!occupancy.entering) {
                    SendLocalExit(false, occupancy.door, true);
                    gLocalRide = LocalRide::OnFoot;
                }
                else if (LocalWantsToAbortEnter()) {
                    SDK::AbortVehicleEnter(0);
                    SendLocalExit(false, occupancy.door, true);
                    gBlockEnterUntilRelease = true;
                    gLocalRide              = LocalRide::OnFoot;
                }
                break;

            case LocalRide::Seated:
                if (gBlockExitUntilRelease) {
                    break;
                }

                if (occupancy.leaving) {
                    SendLocalExit(false, occupancy.door);
                    gLocalRide = LocalRide::Leaving;
                }
                else if (!occupancy.seated && !occupancy.entering) {
                    SendLocalExit(false, occupancy.door);
                    gLocalRide = LocalRide::OnFoot;
                }
                break;

            case LocalRide::Leaving:
                if (!occupancy.seated && !occupancy.leaving) {
                    gLocalRide = LocalRide::OnFoot;
                }
                else if (occupancy.seated && !occupancy.leaving) {
                    if (vehicleId != 0) {
                        SendLocalEnter(vehicleId, occupancy.seat, true, occupancy.door);
                    }

                    gLocalRide = LocalRide::Seated;
                }
                break;
            }
        }

        // MTA's rule, and the one that matters: `if (uiSeat == 0)`.
        //
        // CNetAPI::WriteVehiclePuresync writes rotation, move speed, turn
        // speed, health and the vehicle-specific block ONLY for seat 0, and
        // ReadVehiclePuresync applies them only for seat 0. Being inside a
        // car is not authority over it.
        //
        // GTA's own m_pDriver is the source of truth here rather than the
        // replica's owner flag: a passenger who used to drive this car can
        // still be holding a stale ownerGUID (the server keeps it across an
        // exit so an empty car still rolls), and that is exactly the case
        // that let a passenger steer the driver's bike.
        [[nodiscard]] bool LocalPlayerOwnsVehicleInput(void *handle) {
            if (SDK::IsLocalPlayerDriving(handle)) {
                return true;
            }

            const auto occupancy = SDK::QueryPedOccupancy(0);

            // Seated somewhere in this car, but not at the wheel. Loud,
            // because reaching here means the replica handed us authority we
            // must not act on - a passenger about to write the driver's pose.
            if (occupancy.vehicle == handle && occupancy.seated && occupancy.seat != SDK::kDriverSeat) {
                static std::uint32_t lastWarnMs = 0;
                const std::uint32_t  now        = GetTickCount();

                if (lastWarnMs == 0 || (now - lastWarnMs) > 1000u) {
                    lastWarnMs = now;
                    Framework::Logging::GetLogger("Vehicle")
                        ->warn("Refusing driver authority: local player is a passenger in seat {} of vehicle {}", occupancy.seat,
                               VehicleNetworkIdOf(handle));
                }
            }

            return false;
        }

        void PushOwnedVehicle(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle) {
                return;
            }

            // A PASSENGER NEVER WRITES THE CAR. Pose, momentum, steer and
            // pedals below are all driver-only state; letting a passenger
            // push them is the authority bug, not a cosmetic one.
            if (!LocalPlayerOwnsVehicleInput(remote.handle)) {
                return;
            }

            const auto state = SDK::ReadVehicle(remote.handle);

            entity->position        = glm::vec3(state.x, state.y, state.z);
            entity->velocity        = glm::vec3(state.vx, state.vy, state.vz);
            entity->rotation        = state.rotation;
            entity->angularVelocity = glm::vec3(state.avx, state.avy, state.avz);
            entity->health          = state.health;
            entity->engineOn        = state.engineOn;
            entity->sirenOn         = state.sirenOn;

            if (entity->blown || SDK::IsVehicleWrecked(remote.handle) || state.health <= Shared::VehicleEntity::kWreckHealth) {
                entity->LatchBlown();
                SDK::KeepScriptVehicle(remote.handle);
            }
            entity->steer           = state.steer;
            entity->accelerator     = state.accelerator;
            entity->brake           = state.brake;

            if (state.hasDamage) {
                entity->damageEngine     = state.damageEngine;
                entity->doorsPacked      = state.doorsPacked;
                entity->wheelsPacked     = state.wheelsPacked;
                entity->panels           = state.panels;
                entity->lights           = state.lights;
                entity->doorRatiosPacked = state.doorRatiosPacked;
            }

            entity->interior = SDK::VehicleInterior(remote.handle);

            // Landing gear is a GTA fact on planes (the key toggles it).
            // Cars stay true and this read is a no-op.
            entity->landingGearDown = SDK::VehicleLandingGearDown(remote.handle);
            entity->lightsOn        = SDK::VehicleLightsOn(remote.handle);
            entity->onGround        = SDK::VehicleOnGround(remote.handle);
        }

        void ApplyCosmetics(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle) {
                return;
            }

            const bool colours = !remote.haveColours || remote.lastColours != entity->colours;
            const bool paint   = remote.lastPaintjob != entity->paintjob;
            const bool parts   = remote.lastUpgrades != entity->upgrades;

            if (!colours && !paint && !parts) {
                return;
            }

            if (colours) {
                SDK::SetVehicleRgbColors(remote.handle, entity->colours.data(), Shared::kVehicleColorCount);
                remote.lastColours = entity->colours;
                remote.haveColours = true;
            }

            if (paint || parts) {
                SDK::ApplyVehicleCosmetics(remote.handle, entity->paintjob, entity->upgrades.data(), Shared::kUpgradeSlots);
                remote.lastPaintjob  = entity->paintjob;
                remote.lastUpgrades  = entity->upgrades;
            }
        }

        void ApplyScriptState(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle) {
                return;
            }

            if (remote.haveScriptState && remote.lastEngineOn == entity->engineOn && remote.lastSirenOn == entity->sirenOn &&
                remote.lastOverrideLights == entity->overrideLights && remote.lastLocked == entity->locked) {
                return;
            }

            SDK::ApplyVehicleScriptState(remote.handle, entity->engineOn, entity->sirenOn, entity->overrideLights, entity->locked);
            remote.lastEngineOn       = entity->engineOn;
            remote.lastSirenOn        = entity->sirenOn;
            remote.lastOverrideLights = entity->overrideLights;
            remote.lastLocked         = entity->locked;
            remote.haveScriptState    = true;
        }

        void ApplyExtras(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle) {
                return;
            }

            if (remote.haveExtras && remote.lastDirt == entity->dirtLevel && remote.lastTaxiLight == entity->taxiLight &&
                remote.lastHeadlight == entity->headlightColor && remote.lastFrozen == entity->frozen && remote.lastDamageProof == entity->damageProof &&
                remote.lastCollisions == entity->collisions && remote.lastLandingGear == entity->landingGearDown && remote.lastLightsOn == entity->lightsOn &&
                remote.lastEngineBroken == entity->engineBroken && remote.lastWheelsCanBurst == entity->wheelsCanBurst &&
                remote.lastCanBeDamaged == entity->canBeDamaged && remote.lastNitroCount == entity->nitroCount && remote.lastPlateText == entity->plateText) {
                return;
            }

            SDK::ApplyVehicleExtras(remote.handle, entity->dirtLevel, entity->taxiLight, entity->headlightColor, entity->frozen, entity->damageProof,
                                    entity->collisions, entity->landingGearDown, entity->lightsOn, entity->engineBroken, entity->wheelsCanBurst,
                                    entity->canBeDamaged, entity->nitroCount);
            SDK::SetVehiclePlateText(remote.handle, entity->plateText);
            remote.lastDirt           = entity->dirtLevel;
            remote.lastTaxiLight      = entity->taxiLight;
            remote.lastHeadlight      = entity->headlightColor;
            remote.lastFrozen         = entity->frozen;
            remote.lastDamageProof    = entity->damageProof;
            remote.lastCollisions     = entity->collisions;
            remote.lastLandingGear    = entity->landingGearDown;
            remote.lastLightsOn       = entity->lightsOn;
            remote.lastEngineBroken   = entity->engineBroken;
            remote.lastWheelsCanBurst = entity->wheelsCanBurst;
            remote.lastCanBeDamaged   = entity->canBeDamaged;
            remote.lastNitroCount     = entity->nitroCount;
            remote.lastPlateText      = entity->plateText;
            remote.haveExtras         = true;
        }

        bool DamageWorseThan(const SDK::VehicleState &local, const Shared::VehicleEntity &entity) {
            if (local.health + 0.5f < entity.health) {
                return true;
            }

            if (local.damageEngine > entity.damageEngine) {
                return true;
            }

            if (Shared::VehicleEntity::WorsePacked(local.panels, entity.panels, Shared::VehicleEntity::kPanelCount, 4) != entity.panels) {
                return true;
            }

            if (Shared::VehicleEntity::WorsePacked(local.lights, entity.lights, Shared::VehicleEntity::kLightCount, 2) != entity.lights) {
                return true;
            }

            if (Shared::VehicleEntity::WorseBytes(local.doorsPacked, entity.doorsPacked, Shared::VehicleEntity::kDoorCount) != entity.doorsPacked) {
                return true;
            }

            return Shared::VehicleEntity::WorseBytes(local.wheelsPacked, entity.wheelsPacked, Shared::VehicleEntity::kWheelCount) != entity.wheelsPacked;
        }

        void ApplyDamageToHandle(void *handle, const Shared::VehicleEntity &entity, RemoteVehicle &remote, bool spawnFlying, bool applyRatios) {
            SDK::ApplyVehicleDamage(handle, entity.damageEngine, entity.doorsPacked, entity.wheelsPacked, entity.panels, entity.lights, spawnFlying);

            if (applyRatios) {
                SDK::ApplyVehicleDoorRatios(handle, entity.doorRatiosPacked);
                remote.lastRatios = entity.doorRatiosPacked;
            }

            remote.haveDamage = true;
            remote.lastEngine = entity.damageEngine;
            remote.lastDoors  = entity.doorsPacked;
            remote.lastWheels = entity.wheelsPacked;
            remote.lastPanels = entity.panels;
            remote.lastLights = entity.lights;
        }

        float RatioAt(std::uint64_t packed, int index) {
            return static_cast<float>((packed >> (index * 8)) & 0xFFu) / 255.0f;
        }

        std::uint64_t PackRatios(const float *ratios) {
            std::uint64_t packed = 0;

            for (int i = 0; i < Shared::VehicleEntity::kDoorCount; ++i) {
                const auto byte = static_cast<std::uint8_t>(std::clamp(ratios[i], 0.0f, 1.0f) * 255.0f);

                packed |= static_cast<std::uint64_t>(byte) << (i * 8);
            }

            return packed;
        }

        void BeginDoorInterp(RemoteVehicle &remote, void *handle, std::uint64_t targetPacked) {
            const auto current = SDK::ReadVehicle(handle);

            for (int i = 0; i < Shared::VehicleEntity::kDoorCount; ++i) {
                remote.doorStart[i]  = RatioAt(current.doorRatiosPacked, i);
                remote.doorTarget[i] = RatioAt(targetPacked, i);
            }

            remote.heldRatios    = targetPacked;
            remote.doorInterpMs  = GetTickCount();
            remote.doorInterp    = true;
        }

        void MergeOwnerDamage(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            const auto local = SDK::ReadVehicle(remote.handle);

            if (!local.hasDamage) {
                return;
            }

            // Status only. Ratios stay on this client - applying the replica
            // here is what made the driver's close bounce back open.
            // No model rebuild: the puncher is often still in contact.
            const bool replicaWorse = entity->damageEngine > local.damageEngine ||
                Shared::VehicleEntity::WorseBytes(entity->doorsPacked, local.doorsPacked, Shared::VehicleEntity::kDoorCount) != local.doorsPacked ||
                Shared::VehicleEntity::WorseBytes(entity->wheelsPacked, local.wheelsPacked, Shared::VehicleEntity::kWheelCount) != local.wheelsPacked ||
                Shared::VehicleEntity::WorsePacked(entity->panels, local.panels, Shared::VehicleEntity::kPanelCount, 4) != local.panels ||
                Shared::VehicleEntity::WorsePacked(entity->lights, local.lights, Shared::VehicleEntity::kLightCount, 2) != local.lights;

            if (replicaWorse) {
                ApplyDamageToHandle(remote.handle, *entity, remote, false, false);
            }

            if (entity->health + 0.5f < local.health) {
                remote.health = entity->health;

                auto motion   = local;
                motion.health = entity->health;
                SDK::ApplyVehicleMotion(remote.handle, motion);
            }
        }

        void ReportBystanderDamage(std::uint64_t vehicleId, Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            const auto local = SDK::ReadVehicle(remote.handle);

            if (!local.hasDamage || !DamageWorseThan(local, *entity)) {
                return;
            }

            const std::uint32_t now = GetTickCount();

            if (remote.lastDamageReportMs != 0 && (now - remote.lastDamageReportMs) < Shared::SyncRate::kVehicleMs) {
                return;
            }

            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            Shared::RPC::VehicleDamageReport report;
            report.FromParameters(vehicleId, local.damageEngine, local.doorsPacked, local.wheelsPacked, local.panels, local.lights, 0, false, local.health);
            net->BroadcastRPC(report);
            remote.lastDamageReportMs = now;
        }

        // GTA eTargetDoor -> CDoor index. Hood/boot are 0/1; the climb
        // uses 2-5. 10 is the same front-left as 0; MTA maps both.
        std::uint8_t DoorIndexFromTarget(std::uint8_t target) {
            switch (target) {
            case 0:
            case 10:
                return 2;
            case 8:
                return 3;
            case 11:
                return 4;
            case 9:
                return 5;
            default:
                return SDK::kInvalidDoor;
            }
        }

        std::uint8_t LocalOccupyingDoorMask(void *vehicle) {
            const auto occupancy = SDK::QueryPedOccupancy(0);

            if (occupancy.vehicle != vehicle || (!occupancy.entering && !occupancy.leaving)) {
                return 0;
            }

            const auto index = DoorIndexFromTarget(occupancy.door);

            if (index == SDK::kInvalidDoor) {
                return 0;
            }

            return static_cast<std::uint8_t>(1u << index);
        }

        void ReportOccupantDoorRatios(std::uint64_t vehicleId, Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle || entity->IsOwner()) {
                return;
            }

            // THE DRIVER OWNS THE SWING. A seated passenger reporting
            // every 100ms overwrote the close they just started, and
            // OpenDoor on the way back is the kick-and-clap loop.
            if (SDK::VehicleHasDriver(remote.handle) || SDK::IsLocalPlayerDriving(remote.handle)) {
                return;
            }

            const auto occupancy = SDK::QueryPedOccupancy(0);

            if (occupancy.vehicle != remote.handle || (!occupancy.entering && !occupancy.leaving)) {
                return;
            }

            const auto local = SDK::ReadVehicle(remote.handle);

            if (local.doorRatiosPacked == remote.lastSentRatios) {
                return;
            }

            const std::uint32_t now = GetTickCount();

            if (remote.lastRatioReportMs != 0 && (now - remote.lastRatioReportMs) < Shared::SyncRate::kVehicleMs) {
                return;
            }

            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            Shared::RPC::VehicleDamageReport report;
            report.FromParameters(vehicleId, local.damageEngine, local.doorsPacked, local.wheelsPacked, local.panels, local.lights, local.doorRatiosPacked, true,
                                  local.health);
            net->BroadcastRPC(report);
            remote.lastRatioReportMs = now;
            remote.lastSentRatios    = local.doorRatiosPacked;
        }

        void PulseRemoteDoorRatios(void *handle, RemoteVehicle &remote) {
            // MTA SetDoorOpenRatio(..., TICK_RATE): lerp over the send
            // interval, then stop. Holding OpenDoor every frame fights
            // GTA on a swinging or damaged door and is the stutter.
            const auto skip = LocalOccupyingDoorMask(handle);

            if (!remote.doorInterp) {
                const auto current = SDK::ReadVehicle(handle);

                for (int i = 0; i < Shared::VehicleEntity::kDoorCount; ++i) {
                    if (std::fabs(RatioAt(current.doorRatiosPacked, i) - RatioAt(remote.heldRatios, i)) > (2.0f / 255.0f)) {
                        BeginDoorInterp(remote, handle, remote.heldRatios);
                        break;
                    }
                }

                if (!remote.doorInterp) {
                    return;
                }
            }

            const std::uint32_t now     = GetTickCount();
            const float         elapsed = static_cast<float>(now - remote.doorInterpMs);
            const float         span    = static_cast<float>(Shared::SyncRate::kVehicleMs);

            if (elapsed >= span) {
                SDK::ApplyVehicleDoorRatios(handle, remote.heldRatios, skip);
                remote.doorInterp = false;
                return;
            }

            float mixed[Shared::VehicleEntity::kDoorCount] {};

            for (int i = 0; i < Shared::VehicleEntity::kDoorCount; ++i) {
                mixed[i] = remote.doorStart[i] + (remote.doorTarget[i] - remote.doorStart[i]) * (elapsed / span);
            }

            SDK::ApplyVehicleDoorRatios(handle, PackRatios(mixed), skip);
        }

        void EnsureSpawned(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (remote.handle) {
                return;
            }

            const int model = static_cast<int>(entity->modelId);

            if (!SDK::IsVehicleModel(model)) {
                return;
            }

            if (!SDK::IsModelLoaded(model)) {
                SDK::RequestModel(model);
                return;
            }

            const float heading = glm::roll(entity->rotation);

            remote.handle = SDK::CreateScriptVehicle(model, entity->position.x, entity->position.y, entity->position.z, heading, entity->primaryColour,
                                                     entity->secondaryColour, entity->variant, entity->variant2);

            if (!remote.handle) {
                Framework::Logging::GetLogger("Vehicle")->warn("Could not create vehicle {} (model {})", static_cast<std::uint64_t>(entity->GetNetworkID()), model);
                return;
            }

            remote.lastVariant  = entity->variant;
            remote.lastVariant2 = entity->variant2;
            remote.haveVariants = true;

            SDK::SetVehiclePose(remote.handle, entity->position.x, entity->position.y, entity->position.z, entity->rotation);
            SDK::ApplyVehicleMotion(remote.handle, SDK::ReadVehicle(remote.handle));

            auto state = SDK::ReadVehicle(remote.handle);

            state.health   = entity->health;
            state.engineOn = entity->engineOn;
            state.sirenOn  = entity->sirenOn;
            state.vx       = entity->velocity.x;
            state.vy       = entity->velocity.y;
            state.vz       = entity->velocity.z;
            state.avx      = entity->angularVelocity.x;
            state.avy      = entity->angularVelocity.y;
            state.avz      = entity->angularVelocity.z;

            SDK::ApplyVehicleMotion(remote.handle, state);
            SDK::ApplyVehicleDamage(remote.handle, entity->damageEngine, entity->doorsPacked, entity->wheelsPacked, entity->panels, entity->lights);
            SDK::ApplyVehicleDoorRatios(remote.handle, entity->doorRatiosPacked);
            SDK::BindRemoteVehicleProcessControl(remote.handle);

            if (entity->IsBlown()) {
                // Already a wreck on the wire. Hide the boom — it happened
                // before this client existed.
                SDK::WreckVehicle(remote.handle, true);
                remote.wrecked = true;
            }

            SDK::KeepScriptVehicle(remote.handle);
            SDK::SetVehicleInterior(remote.handle, entity->interior);
            ApplyCosmetics(entity, remote);
            ApplyScriptState(entity, remote);
            ApplyExtras(entity, remote);

            remote.velocity = entity->velocity;
            remote.angular  = entity->angularVelocity;
            remote.health   = entity->health;
            remote.engineOn = entity->engineOn;
            remote.sirenOn  = entity->sirenOn;
            remote.haveDamage = true;
            remote.lastEngine = entity->damageEngine;
            remote.lastDoors  = entity->doorsPacked;
            remote.lastWheels = entity->wheelsPacked;
            remote.lastPanels = entity->panels;
            remote.lastLights = entity->lights;
            remote.lastRatios       = entity->doorRatiosPacked;
            remote.lastEntityRatios = entity->doorRatiosPacked;
            remote.heldRatios       = entity->doorRatiosPacked;
        }

        // MOMENTUM IS A PER-PACKET FACT, NOT A PER-FRAME ONE.
        //
        // MTA writes SetMoveSpeed / SetTurnSpeed once, where the puresync
        // packet is read (CNetAPI ReadVehiclePuresync, inside `uiSeat == 0`),
        // and then leaves the vehicle to GTA until the next packet.
        //
        // Writing turn speed every frame instead is what over-leans a bike.
        // CBike::ProcessControl derives lean from turn speed and then damps
        // it as part of integrating a frame; re-injecting the same 100ms-old
        // turn speed sixty times a second cancels that damping, so the roll
        // the owner reached once is re-driven every frame and the remote bike
        // ends up dragging a knee. A car hides it because its roll is pinned
        // by four wheels on the ground - a bike's is free.
        //
        // Health / engine / siren stay per-frame: those are states, not
        // momentum, and nothing in GTA integrates them away.
        void ApplyStoredMotion(void *handle, const RemoteVehicle &remote, bool withMomentum) {
            auto motion = SDK::ReadVehicle(handle);

            if (withMomentum) {
                motion.vx  = remote.velocity.x;
                motion.vy  = remote.velocity.y;
                motion.vz  = remote.velocity.z;
                motion.avx = remote.angular.x;
                motion.avy = remote.angular.y;
                motion.avz = remote.angular.z;
            }

            motion.health   = remote.health;
            motion.engineOn = remote.engineOn;
            motion.sirenOn  = remote.sirenOn;

            SDK::ApplyVehicleMotion(handle, motion);
        }

        void ApplyRemoteVehicle(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle || SDK::IsLocalPlayerDriving(remote.handle)) {
                return;
            }

            SDK::SetVehicleInterior(remote.handle, entity->interior);
            ApplyCosmetics(entity, remote);
            ApplyScriptState(entity, remote);
            ApplyExtras(entity, remote);

            const std::uint32_t now     = GetTickCount();
            const bool          changed = !remote.aimed || remote.aimedPosition != entity->position || remote.aimedRotation != entity->rotation;

            remote.health      = entity->health;
            remote.engineOn    = entity->engineOn;
            remote.sirenOn     = entity->sirenOn;
            remote.steer       = entity->steer;
            remote.accelerator = entity->accelerator;
            remote.brake       = entity->brake;

            const bool partsChanged = !remote.haveDamage || remote.lastEngine != entity->damageEngine || remote.lastDoors != entity->doorsPacked ||
                remote.lastWheels != entity->wheelsPacked || remote.lastPanels != entity->panels || remote.lastLights != entity->lights;

            if (partsChanged && !remote.wrecked) {
                // One flying component per newly-missing part, not a
                // SetupDamageAfterLoad of the whole wreck.
                ApplyDamageToHandle(remote.handle, *entity, remote, entity->health > SDK::kWreckHealth, false);
            }

            if (!remote.wrecked && (entity->IsBlown() || SDK::IsVehicleWrecked(remote.handle))) {
                SDK::WreckVehicle(remote.handle, false);
                remote.wrecked = true;
            }

            if (remote.wrecked) {
                // GTA deletes a wreck after 60 seconds and will stream
                // the hull out if BlowUpCar dropped the mission lock.
                // Leave it where the boom put it - interpolating a
                // wreck is how it falls through the map.
                SDK::KeepScriptVehicle(remote.handle);
                return;
            }

            if (entity->doorRatiosPacked != remote.lastEntityRatios) {
                remote.lastEntityRatios = entity->doorRatiosPacked;
                remote.lastRatios       = entity->doorRatiosPacked;
                BeginDoorInterp(remote, remote.handle, entity->doorRatiosPacked);
            }

            if (changed) {
                const auto current = SDK::ReadVehicle(remote.handle);

                remote.velocity = entity->velocity;
                remote.angular  = entity->angularVelocity;

                // MTA SetTargetPosition: error = target - current, plus a
                // few milliseconds of velocity (base 5ms), then scale the
                // error by lerp(0.25, 1, unlerp(100, delay, 400)). A 100ms
                // tick therefore applies a quarter of the gap - we had the
                // range at 0-100 so the whole gap landed in one packet, and
                // we aimed a full send-interval ahead, which is the stutter.
                constexpr float kFramesPerSecond = 50.0f;
                constexpr float kExtrapolateMs   = 5.0f;
                const glm::vec3 target           = entity->position + entity->velocity * (kFramesPerSecond * kExtrapolateMs / 1000.0f);

                remote.interpolator.GetPosition()->SetErrorContributionDelayRange(100.0f, 400.0f);
                remote.interpolator.GetRotation()->SetErrorContributionDelayRange(100.0f, 400.0f);
                remote.interpolator.GetPosition()->SetCompensationFactor(1.5f);
                remote.interpolator.GetPosition()->SetTargetValue(glm::vec3(current.x, current.y, current.z), target, static_cast<float>(Shared::SyncRate::kVehicleMs));
                remote.interpolator.GetRotation()->SetTargetValue(current.rotation, entity->rotation, static_cast<float>(Shared::SyncRate::kVehicleMs));

                remote.aimedPosition = entity->position;
                remote.aimedRotation = entity->rotation;
                remote.speed         = glm::length(entity->velocity);
                remote.whenMs        = now;
                remote.aimed         = true;
            }

            const auto here = SDK::ReadVehicle(remote.handle);
            const glm::vec3 currentPos(here.x, here.y, here.z);
            const float     threshold = Shared::Interpolation::VehicleWarpThresholdFor(remote.speed, Shared::SyncRate::kVehicleMs);

            if (remote.aimed && glm::distance(currentPos, remote.aimedPosition) > threshold) {
                SDK::SetVehiclePose(remote.handle, remote.aimedPosition.x, remote.aimedPosition.y, remote.aimedPosition.z, remote.aimedRotation);
                remote.interpolator.GetPosition()->SetTargetValue(remote.aimedPosition, remote.aimedPosition, static_cast<float>(Shared::SyncRate::kVehicleMs));
                remote.interpolator.GetRotation()->SetTargetValue(remote.aimedRotation, remote.aimedRotation, static_cast<float>(Shared::SyncRate::kVehicleMs));
            }
            else {
                const auto nextPos = remote.interpolator.GetPosition()->UpdateTargetValue(currentPos);
                const auto nextRot = remote.interpolator.GetRotation()->UpdateTargetValue(here.rotation);

                SDK::SetVehiclePose(remote.handle, nextPos.x, nextPos.y, nextPos.z, nextRot);
            }

            // AFTER THE POSE. SetPosn and collision from the correction
            // can kill the speed the game just integrated, so momentum goes
            // back on the frame a packet arrived - but only on that frame.
            // See ApplyStoredMotion: re-writing turn speed every frame is
            // what over-leaned a synchronised bike.
            ApplyStoredMotion(remote.handle, remote, changed);

            // STEER IS AN OUTPUT. ProcessControl rebuilds m_fSteerAngle
            // from the swapped pad. Writing the last packet's quantized
            // angle over that every frame is the cabin snap when they
            // turn - GTA had already ramped the wheels and this put
            // them back. Pedals come from the same pad.

            PulseRemoteDoorRatios(remote.handle, remote);
        }

        void DestroyHandle(RemoteVehicle &remote) {
            if (!remote.handle) {
                return;
            }

            SDK::CancelEntriesInto(remote.handle);
            SDK::ClearVehicleRgbColors(remote.handle);
            SDK::ClearVehicleHeadlightColor(remote.handle);
            SDK::DestroyScriptVehicle(remote.handle);
            remote.handle = nullptr;
        }

        void RecreateIfVariantChanged(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle || !remote.haveVariants) {
                return;
            }

            if (remote.lastVariant == entity->variant && remote.lastVariant2 == entity->variant2) {
                return;
            }

            // Extras are chosen at CreateCarForScript. Changing them means
            // a new clump — same as MTA SetVariant → ReCreate.
            DestroyHandle(remote);
            remote.haveExtras       = false;
            remote.haveScriptState  = false;
            remote.haveColours      = false;
            remote.haveVariants     = false;
            remote.haveDamage       = false;
            remote.wrecked          = false;
        }
    } // namespace

    void UpdateVehicleSync() {
        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication) {
            return;
        }

        std::unordered_set<std::uint64_t> seen;

        replication->ForEachEntity([&seen](Replication::NetworkEntity *entity) {
            auto *vehicle = dynamic_cast<Shared::VehicleEntity *>(entity);

            if (!vehicle) {
                return;
            }

            const auto id = static_cast<std::uint64_t>(vehicle->GetNetworkID());

            if (!Shared::SameDimension(QueryLocalWorldSpace().dimension, vehicle->dimension)) {
                return;
            }

            seen.insert(id);

            auto &remote = gVehicles[id];

            RecreateIfVariantChanged(vehicle, remote);
            EnsureSpawned(vehicle, remote);

            if (!remote.handle) {
                return;
            }

            if (vehicle->IsOwner()) {
                ApplyCosmetics(vehicle, remote);
                ApplyScriptState(vehicle, remote);
                ApplyExtras(vehicle, remote);
                SDK::SetVehicleAlpha(remote.handle, vehicle->alpha);
                MergeOwnerDamage(vehicle, remote);
                PushOwnedVehicle(vehicle, remote);
                return;
            }

            ReportBystanderDamage(id, vehicle, remote);
            ReportOccupantDoorRatios(id, vehicle, remote);
            ApplyRemoteVehicle(vehicle, remote);
            SDK::SetVehicleAlpha(remote.handle, vehicle->alpha);
        });

        for (auto it = gVehicles.begin(); it != gVehicles.end();) {
            if (seen.contains(it->first)) {
                ++it;
                continue;
            }

            DestroyHandle(it->second);
            it = gVehicles.erase(it);
        }

        WatchLocalOccupancy();
    }

    void ClearRemoteVehicles() {
        for (auto &[id, remote] : gVehicles) {
            DestroyHandle(remote);
        }

        gVehicles.clear();
        gLateJoinWarp.clear();
        gLocalRide = LocalRide::OnFoot;
    }

    std::uint64_t VehicleNetworkIdOf(const void *vehicle) {
        if (!vehicle) {
            return 0;
        }

        for (const auto &[id, remote] : gVehicles) {
            if (remote.handle == vehicle) {
                return id;
            }
        }

        return 0;
    }

    void SeatRemoteInVehicle(int slot, std::uint64_t vehicleId, std::uint8_t seat, bool lateJoin) {
        if (lateJoin) {
            RememberLateJoin(slot);
        }

        auto *handle = HandleOf(vehicleId);

        if (!handle) {
            return;
        }

        const auto occupancy = SDK::QueryPedOccupancy(slot);

        if (occupancy.seated && occupancy.vehicle == handle && occupancy.seat == seat) {
            ForgetLateJoin(slot);
            return;
        }

        if (occupancy.entering && occupancy.vehicle == handle) {
            return;
        }

        if (occupancy.leaving) {
            return;
        }

        // MTA puresync never seats anyone. A live climb is PlayerEnterVehicle
        // with the door. Starting a walk from this snapshot - before that
        // RPC, and without a door - is the frozen walk-to-car: they path
        // from wherever they are, then the RPC is ignored because they
        // already "occupy" the vehicle.
        const bool warp = lateJoin || ConsumeLateJoin(slot);

        if (!warp) {
            return;
        }

        if (SDK::EnterVehicle(slot, handle, seat, true)) {
            ForgetLateJoin(slot);
        }
    }

    void ApplyRemoteEnterVehicle(std::uint64_t networkId, std::uint64_t vehicleId, std::uint8_t seat, bool warp, std::uint8_t door) {
        if (networkId == 0 || networkId == LocalNetworkId()) {
            if (networkId == LocalNetworkId() && warp) {
                auto *handle = HandleOf(vehicleId);

                if (handle) {
                    SDK::EnterVehicle(0, handle, seat, true, door);
                    gLocalRide = LocalRide::Seated;
                }
            }

            return;
        }

        const int slot = SlotOfNetworkId(networkId);

        if (slot < 0) {
            return;
        }

        if (warp) {
            RememberLateJoin(slot);
        }
        else {
            ForgetLateJoin(slot);
        }

        auto *handle = HandleOf(vehicleId);

        if (!handle) {
            return;
        }

        SDK::EnterVehicle(slot, handle, seat, warp, door);

        // Collision stays on while they walk to the door. Turning it off
        // here is what froze remotes mid-stride: the enter task cannot
        // path to a door on a ped that does not collide.
        if (SDK::IsPedInVehicle(slot)) {
            SDK::SetRemotePedCollision(slot, false);
        }
    }

    void ApplyRemoteExitVehicle(std::uint64_t networkId, bool warp, std::uint8_t door) {
        if (networkId == 0 || networkId == LocalNetworkId()) {
            if (networkId == LocalNetworkId() && warp) {
                SDK::ExitVehicle(0, true, door);
                gLocalRide = LocalRide::OnFoot;
            }

            return;
        }

        const int slot = SlotOfNetworkId(networkId);

        if (slot < 0) {
            return;
        }

        ForgetLateJoin(slot);
        SDK::ExitVehicle(slot, warp, door);

        if (!SDK::IsPedInVehicle(slot)) {
            SDK::SetRemotePedCollision(slot, true);
        }
    }

    void ApplyOccupancyAbort(std::uint64_t networkId, std::uint64_t vehicleId, std::uint8_t kind, std::uint8_t seat, std::uint8_t door) {
        (void)door;

        const bool local = networkId != 0 && networkId == LocalNetworkId();
        const int  slot  = local ? 0 : SlotOfNetworkId(networkId);

        if (!local && slot < 0) {
            return;
        }

        auto *handle = HandleOf(vehicleId);

        if (kind == Shared::RPC::PlayerOccupancyAbort::kExit) {
            SDK::AbortVehicleExit(slot, handle, seat);

            if (local) {
                gBlockExitUntilRelease = true;
                gLocalRide             = LocalRide::Seated;
            }

            return;
        }

        SDK::AbortVehicleEnter(slot);
        ForgetLateJoin(slot);

        if (local) {
            gBlockEnterUntilRelease = true;
            gLocalRide              = LocalRide::OnFoot;
        }
        else if (!SDK::IsPedInVehicle(slot)) {
            SDK::SetRemotePedCollision(slot, true);
        }
    }

    void ClearRemoteOccupancy(int slot) {
        ForgetLateJoin(slot);
    }

    void ApplyRemoteVehicleDamage(std::uint64_t vehicleId, std::uint8_t engine, std::uint64_t doors, std::uint32_t wheels, std::uint32_t panels, std::uint32_t lights,
                                  std::uint64_t doorRatios, bool applyRatios, float health) {
        const auto found = gVehicles.find(vehicleId);

        if (found == gVehicles.end() || !found->second.handle) {
            return;
        }

        Shared::VehicleEntity entity;
        entity.damageEngine     = engine;
        entity.doorsPacked      = doors;
        entity.wheelsPacked     = wheels;
        entity.panels           = panels;
        entity.lights           = lights;
        entity.doorRatiosPacked = doorRatios;
        entity.health           = health;

        ApplyDamageToHandle(found->second.handle, entity, found->second, health > SDK::kWreckHealth, false);

        auto &remote = found->second;
        const auto local = SDK::ReadVehicle(remote.handle);

        if (health + 0.5f < local.health) {
            remote.health = health;

            auto motion   = local;
            motion.health = health;
            SDK::ApplyVehicleMotion(remote.handle, motion);
        }

        if (!remote.wrecked && health <= SDK::kWreckHealth) {
            SDK::WreckVehicle(remote.handle, false);
            remote.wrecked = true;
        }

        if (remote.wrecked) {
            SDK::KeepScriptVehicle(remote.handle);
        }

        if (applyRatios) {
            remote.lastRatios       = doorRatios;
            remote.lastEntityRatios = doorRatios;
            BeginDoorInterp(remote, remote.handle, doorRatios);
        }
    }

    void TryLocalPassengerKey() {
        if (SDK::QueryLocalPedWeapon().reloading) {
            return;
        }

        const auto occupancy = SDK::QueryPedOccupancy(0);

        if (occupancy.entering || occupancy.leaving) {
            return;
        }

        // MTA enter_passenger: G gets a passenger out. A driver has to
        // use F. Old Vanta exited from every seat; that is not what G
        // means here.
        if (occupancy.seated) {
            if (occupancy.seat != SDK::kDriverSeat) {
                SDK::ExitVehicle(0, false);
            }

            return;
        }

        void *nearest          = nullptr;
        float nearestDistance = SDK::kPassengerReach;

        for (auto &entry : gVehicles) {
            auto &remote = entry.second;
            float distance = 0.0f;

            if (!remote.handle || !SDK::PassengerDoorDistance(0, remote.handle, &distance)) {
                continue;
            }

            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest         = remote.handle;
            }
        }

        if (!nearest) {
            return;
        }

        const auto seat = SDK::FreePassengerSeat(nearest);

        if (seat == 0) {
            return;
        }

        // Walk, not warp. WatchLocalOccupancy sends the same enter RPC
        // the F path uses, so G and F cannot disagree about the climb.
        SDK::EnterVehicle(0, nearest, seat, false);
    }

    void ApplyOwnedVehicleWorldSpace(std::uint64_t networkId, std::uint8_t interior, std::uint16_t dimension, std::uint8_t flags) {
        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication || networkId == 0) {
            return;
        }

        Shared::VehicleEntity *vehicle = nullptr;

        replication->ForEachEntity([&](Replication::NetworkEntity *entity) {
            auto *candidate = dynamic_cast<Shared::VehicleEntity *>(entity);

            if (candidate && candidate->IsOwner() && static_cast<std::uint64_t>(candidate->GetNetworkID()) == networkId) {
                vehicle = candidate;
            }
        });

        if (!vehicle) {
            return;
        }

        if ((flags & Shared::kWorldSpaceInterior) != 0) {
            vehicle->interior = interior;

            const auto found = gVehicles.find(networkId);

            if (found != gVehicles.end() && found->second.handle) {
                SDK::SetVehicleInterior(found->second.handle, interior);
            }
        }

        if ((flags & Shared::kWorldSpaceDimension) != 0) {
            vehicle->dimension = dimension;
        }
    }

    void *VehicleHandleOf(std::uint64_t networkId) {
        const auto found = gVehicles.find(networkId);

        return found == gVehicles.end() ? nullptr : found->second.handle;
    }
} // namespace VantaMP::Game
