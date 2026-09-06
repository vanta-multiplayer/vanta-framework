#include "game/sync/vehicle_sync.h"

#include "game/sync/player_sync.h"
#include "sdk/game.h"
#include "sdk/streaming.h"
#include "shared/player_entity.h"
#include "shared/rpc/player_enter_vehicle.h"
#include "shared/rpc/player_exit_vehicle.h"
#include "shared/rpc/vehicle_damage_report.h"
#include "shared/sync_rates.h"
#include "shared/vehicle_entity.h"

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <utils/interpolator.h>

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
            bool          haveDamage  = false;
            std::uint32_t lastDamageReportMs = 0;
            std::uint32_t lastRatioReportMs  = 0;
            std::uint64_t lastSentRatios     = 0;
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

        void SendLocalExit(bool warp, std::uint8_t door) {
            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            Shared::RPC::PlayerExitVehicle exit;
            exit.FromParameters(0, warp, door);
            net->BroadcastRPC(exit);

            Framework::Logging::GetLogger("Vehicle")->debug("local exit door {} warp {}", door, warp);
        }

        void WatchLocalOccupancy() {
            const auto occupancy = SDK::QueryPedOccupancy(0);
            const auto vehicleId = VehicleNetworkIdOf(occupancy.vehicle);

            switch (gLocalRide) {
            case LocalRide::OnFoot:
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
                    SendLocalExit(false, occupancy.door);
                    gLocalRide = LocalRide::OnFoot;
                }
                break;

            case LocalRide::Seated:
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

        void PushOwnedVehicle(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle) {
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
        }

        std::uint64_t WorseBytes(std::uint64_t left, std::uint64_t right, int count) {
            std::uint64_t worse = 0;

            for (int i = 0; i < count; ++i) {
                const auto shift = static_cast<std::uint64_t>(i * 8);
                const auto a     = static_cast<std::uint8_t>((left >> shift) & 0xFFu);
                const auto b     = static_cast<std::uint8_t>((right >> shift) & 0xFFu);

                worse |= static_cast<std::uint64_t>(a > b ? a : b) << shift;
            }

            return worse;
        }

        bool DamageWorseThan(const SDK::VehicleState &local, const Shared::VehicleEntity &entity) {
            if (local.damageEngine > entity.damageEngine || local.panels > entity.panels || local.lights > entity.lights) {
                return true;
            }

            if (WorseBytes(local.doorsPacked, entity.doorsPacked, Shared::VehicleEntity::kDoorCount) != entity.doorsPacked) {
                return true;
            }

            return WorseBytes(local.wheelsPacked, entity.wheelsPacked, Shared::VehicleEntity::kWheelCount) != entity.wheelsPacked;
        }

        void ApplyDamageToHandle(void *handle, const Shared::VehicleEntity &entity, RemoteVehicle &remote, bool rebuildModel, bool applyRatios) {
            SDK::ApplyVehicleDamage(handle, entity.damageEngine, entity.doorsPacked, entity.wheelsPacked, entity.panels, entity.lights, rebuildModel);

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

        void MergeOwnerDamage(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            const auto local = SDK::ReadVehicle(remote.handle);

            if (!local.hasDamage) {
                return;
            }

            // Status only. Ratios stay on this client - applying the replica
            // here is what made the driver's close bounce back open.
            // No model rebuild: the puncher is often still in contact.
            const bool replicaWorse = entity->damageEngine > local.damageEngine ||
                WorseBytes(entity->doorsPacked, local.doorsPacked, Shared::VehicleEntity::kDoorCount) != local.doorsPacked ||
                WorseBytes(entity->wheelsPacked, local.wheelsPacked, Shared::VehicleEntity::kWheelCount) != local.wheelsPacked ||
                entity->panels > local.panels || entity->lights > local.lights;

            if (replicaWorse) {
                ApplyDamageToHandle(remote.handle, *entity, remote, false, false);
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
            report.FromParameters(vehicleId, local.damageEngine, local.doorsPacked, local.wheelsPacked, local.panels, local.lights, 0, false);
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
            report.FromParameters(vehicleId, local.damageEngine, local.doorsPacked, local.wheelsPacked, local.panels, local.lights, local.doorRatiosPacked, true);
            net->BroadcastRPC(report);
            remote.lastRatioReportMs = now;
            remote.lastSentRatios    = local.doorRatiosPacked;
        }

        void HoldRemoteDoorRatios(void *handle, std::uint64_t packed) {
            // EVERY FRAME, NOT ON CHANGE. GTA shuts swinging doors on its
            // own, so a single OpenDoor from a parked-car snapshot is gone
            // by the next tick. MTA writes the syncer's ratios every
            // puresync. Skip the door the local ped is climbing through.
            SDK::ApplyVehicleDoorRatios(handle, packed, LocalOccupyingDoorMask(handle));
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

            remote.handle = SDK::CreateScriptVehicle(model, entity->position.x, entity->position.y, entity->position.z, heading, entity->primaryColour, entity->secondaryColour);

            if (!remote.handle) {
                Framework::Logging::GetLogger("Vehicle")->warn("Could not create vehicle {} (model {})", static_cast<std::uint64_t>(entity->GetNetworkID()), model);
                return;
            }

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

            Framework::Logging::GetLogger("Vehicle")->info("Vehicle {} (model {}) spawned", static_cast<std::uint64_t>(entity->GetNetworkID()), model);
        }

        void ApplyStoredMotion(void *handle, const RemoteVehicle &remote) {
            SDK::VehicleState motion;
            motion.vx       = remote.velocity.x;
            motion.vy       = remote.velocity.y;
            motion.vz       = remote.velocity.z;
            motion.avx      = remote.angular.x;
            motion.avy      = remote.angular.y;
            motion.avz      = remote.angular.z;
            motion.health   = remote.health;
            motion.engineOn = remote.engineOn;
            motion.sirenOn  = remote.sirenOn;

            SDK::ApplyVehicleMotion(handle, motion);
        }

        void ApplyRemoteVehicle(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle || SDK::IsLocalPlayerDriving(remote.handle)) {
                return;
            }

            const std::uint32_t now     = GetTickCount();
            const bool          changed = !remote.aimed || remote.aimedPosition != entity->position || remote.aimedRotation != entity->rotation;

            if (changed) {
                const auto current = SDK::ReadVehicle(remote.handle);

                remote.velocity = entity->velocity;
                remote.angular  = entity->angularVelocity;
                remote.health   = entity->health;
                remote.engineOn = entity->engineOn;
                remote.sirenOn  = entity->sirenOn;
                remote.steer       = entity->steer;
                remote.accelerator = entity->accelerator;
                remote.brake       = entity->brake;

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

                ApplyStoredMotion(remote.handle, remote);

                const bool partsChanged = !remote.haveDamage || remote.lastEngine != entity->damageEngine || remote.lastDoors != entity->doorsPacked ||
                    remote.lastWheels != entity->wheelsPacked || remote.lastPanels != entity->panels || remote.lastLights != entity->lights;

                if (partsChanged) {
                    ApplyDamageToHandle(remote.handle, *entity, remote, true, false);
                }

                remote.lastRatios       = entity->doorRatiosPacked;
                remote.lastEntityRatios = entity->doorRatiosPacked;
                remote.heldRatios       = entity->doorRatiosPacked;

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
            // can kill the speed the game just integrated. MTA writes
            // SetMoveSpeed on the packet and leaves it on the vehicle;
            // we put it back every frame so physics coasts between ticks.
            ApplyStoredMotion(remote.handle, remote);

            // STEER IS AN OUTPUT. ProcessControl rebuilds m_fSteerAngle
            // from the swapped pad. Writing the last packet's quantized
            // angle over that every frame is the cabin snap when they
            // turn - GTA had already ramped the wheels and this put
            // them back. Pedals come from the same pad.

            if (entity->doorRatiosPacked != remote.lastEntityRatios) {
                remote.lastEntityRatios = entity->doorRatiosPacked;
                remote.lastRatios       = entity->doorRatiosPacked;
                remote.heldRatios       = entity->doorRatiosPacked;
            }

            HoldRemoteDoorRatios(remote.handle, remote.heldRatios);
        }

        void DestroyHandle(RemoteVehicle &remote) {
            if (!remote.handle) {
                return;
            }

            SDK::CancelEntriesInto(remote.handle);
            SDK::DestroyScriptVehicle(remote.handle);
            remote.handle = nullptr;
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

            seen.insert(id);

            auto &remote = gVehicles[id];

            EnsureSpawned(vehicle, remote);

            if (!remote.handle) {
                return;
            }

            if (vehicle->IsOwner()) {
                MergeOwnerDamage(vehicle, remote);
                PushOwnedVehicle(vehicle, remote);
                return;
            }

            ReportBystanderDamage(id, vehicle, remote);
            ReportOccupantDoorRatios(id, vehicle, remote);
            ApplyRemoteVehicle(vehicle, remote);
        });

        for (auto it = gVehicles.begin(); it != gVehicles.end();) {
            if (seen.contains(it->first)) {
                ++it;
                continue;
            }

            Framework::Logging::GetLogger("Vehicle")->info("Vehicle {} streamed out", it->first);
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

    void ClearRemoteOccupancy(int slot) {
        ForgetLateJoin(slot);
    }

    void ApplyRemoteVehicleDamage(std::uint64_t vehicleId, std::uint8_t engine, std::uint64_t doors, std::uint32_t wheels, std::uint32_t panels, std::uint32_t lights,
                                  std::uint64_t doorRatios, bool applyRatios) {
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

        ApplyDamageToHandle(found->second.handle, entity, found->second, false, false);

        if (applyRatios) {
            found->second.lastRatios = doorRatios;
            found->second.heldRatios = doorRatios;
            HoldRemoteDoorRatios(found->second.handle, doorRatios);
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
            Framework::Logging::GetLogger("Vehicle")->info("No vehicle within reach to board as passenger");
            return;
        }

        const auto seat = SDK::FreePassengerSeat(nearest);

        if (seat == 0) {
            Framework::Logging::GetLogger("Vehicle")->info("Nearest vehicle has no free passenger seat");
            return;
        }

        // Walk, not warp. WatchLocalOccupancy sends the same enter RPC
        // the F path uses, so G and F cannot disagree about the climb.
        SDK::EnterVehicle(0, nearest, seat, false);
    }
} // namespace VantaMP::Game
