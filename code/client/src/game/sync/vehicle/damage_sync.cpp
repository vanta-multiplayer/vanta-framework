#include "game/sync/vehicle_sync.h"

#include "game/sync/vehicle/state.h"
#include "sdk/game.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/rpc/vehicle/damage.h"
#include "shared/sync_rates.h"

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <algorithm>
#include <cmath>

// VEHICLE DAMAGE AND DOOR SWING.
//
// Damage is an EVENT, not motion state: it changes rarely and must not be
// lost, so it rides its own reliable packet instead of the 100 ms motion
// stream. Door ratios are the swing angle, which is a different fact from the
// damage status bytes - a popped hood is almost always a ratio, not a flag.
//
// Split out of vehicle_sync.cpp. Shared registry state is in vehicle/state.h.
namespace VantaMP::Game::VehicleSyncState {

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

    // Damage STATUS only. The door swing is driven by BeginDoorInterp on
    // the kDoorRatios path, never from here.
    // (declared in vehicle/state.h)
    void ApplyDamageToHandle(void *handle, const Shared::VehicleEntity &entity, RemoteVehicle &remote, bool spawnFlying) {
        SDK::ApplyVehicleDamage(handle, entity.damageEngine, entity.doorsPacked, entity.wheelsPacked, entity.panels, entity.lights, spawnFlying);

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

    // (declared in vehicle/state.h)
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

    // (declared in vehicle/state.h)
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
            ApplyDamageToHandle(remote.handle, *entity, remote, false);
        }

        if (entity->health + 0.5f < local.health) {
            remote.health = entity->health;

            // States only - see SDK::ApplyVehicleStates. A
            // read-modify-write here would re-impose stale momentum.
            SDK::ApplyVehicleStates(remote.handle, entity->health, local.engineOn, local.sirenOn);
        }
    }

    // (declared in vehicle/state.h)
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

        namespace Field = Shared::RPC::Vehicle::DamageField;

        // STATUS GROUPS ONLY. The door ANGLES are not ours to report -
        // the driver owns the swing - so kDoorRatios stays out of the
        // mask, and the receiver cannot mistake its absence for "shut
        // every door".
        Shared::RPC::VehicleDamageReport report;

        report.vehicleId    = vehicleId;
        report.damageEngine = local.damageEngine;
        report.doorsPacked  = local.doorsPacked;
        report.wheelsPacked = local.wheelsPacked;
        report.panels       = local.panels;
        report.lights       = local.lights;
        report.health       = local.health;
        report.Mark(Field::kEngine);
        report.Mark(Field::kDoors);
        report.Mark(Field::kWheels);
        report.Mark(Field::kPanels);
        report.Mark(Field::kLights);
        report.Mark(Field::kHealth);

        net->BroadcastRPC(report);
        remote.lastDamageReportMs = now;
    }

    // WAS LOCAL-ONLY, AND THAT WAS THE DOOR SPAM.
    //
    // This used to ask QueryPedOccupancy(0) - slot 0, the local player -
    // so only OUR door was protected from network ratio writes. A remote
    // player climbing in or out had their door written over on every
    // packet, and CVehicle::OpenDoor re-plays the swing sound each time
    // it is called with a changed ratio.
    //
    // SDK::OccupiedDoorMask walks the local ped, the vehicle's occupants
    // and anyone entering it, so the same protection MTA gives every
    // occupied door (CClientVehicle::SetDoorOpenRatio) now applies here.
    std::uint8_t OccupyingDoorMask(void *vehicle) {
        return SDK::OccupiedDoorMask(vehicle);
    }

    // (declared in vehicle/state.h)
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

        // KEEP REPORTING AFTER THE TASK ENDS. THIS IS THE ABORT DOOR BUG.
        //
        // The window used to be exactly "while entering or leaving", which
        // closes the frame the enter task dies. But GTA swings the door
        // shut AFTER that - the close is part of the ped returning to
        // foot, not part of the enter task - so the last ratio anyone else
        // heard was a stale half-open angle from mid-climb.
        //
        // The other clients then had no way to correct it. Their own
        // OccupiedDoorMask stops masking the door the moment our ped stops
        // using it, but the only authority for its angle is us, and we had
        // stopped talking. So the aborting player saw the door shut and
        // everyone else kept it hanging open.
        //
        // MTA's answer is to put the ratio in the abort packet
        // (CClientPed.cpp:10443). That is one sample at abort time, which
        // is a fix for the packet but not for the swing that follows it.
        // Holding the window open for a beat afterwards reports the whole
        // close, and costs nothing when the door was already shut because
        // the unchanged-ratio check below drops those.
        constexpr std::uint32_t kDoorSettleMs = 1200;

        const auto          occupancy = SDK::QueryPedOccupancy(0);
        const std::uint32_t now       = GetTickCount();
        const bool          usingDoor = occupancy.vehicle == remote.handle && (occupancy.entering || occupancy.leaving);

        if (usingDoor) {
            remote.doorReleasedMs = now;
        }
        else if (remote.doorReleasedMs == 0 || (now - remote.doorReleasedMs) > kDoorSettleMs) {
            return;
        }

        const auto local = SDK::ReadVehicle(remote.handle);

        if (local.doorRatiosPacked == remote.lastSentRatios) {
            return;
        }

        if (remote.lastRatioReportMs != 0 && (now - remote.lastRatioReportMs) < Shared::SyncRate::kVehicleMs) {
            return;
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        namespace Field = Shared::RPC::Vehicle::DamageField;

        // THE DOOR ANGLES ARE THE POINT OF THIS REPORT, so kDoorRatios
        // is in the mask.
        Shared::RPC::VehicleDamageReport report;

        report.vehicleId        = vehicleId;
        report.damageEngine     = local.damageEngine;
        report.doorsPacked      = local.doorsPacked;
        report.wheelsPacked     = local.wheelsPacked;
        report.panels           = local.panels;
        report.lights           = local.lights;
        report.doorRatiosPacked = local.doorRatiosPacked;
        report.health           = local.health;
        report.Mark(Field::kEngine);
        report.Mark(Field::kDoors);
        report.Mark(Field::kWheels);
        report.Mark(Field::kPanels);
        report.Mark(Field::kLights);
        report.Mark(Field::kDoorRatios);
        report.Mark(Field::kHealth);

        net->BroadcastRPC(report);
        remote.lastRatioReportMs = now;
        remote.lastSentRatios    = local.doorRatiosPacked;
    }

    // (declared in vehicle/state.h)
    void PulseRemoteDoorRatios(void *handle, RemoteVehicle &remote) {
        // MTA SetDoorOpenRatio(..., TICK_RATE): lerp over the send
        // interval, then stop. Holding OpenDoor every frame fights
        // GTA on a swinging or damaged door and is the stutter.
        const auto skip = OccupyingDoorMask(handle);

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

} // namespace VantaMP::Game::VehicleSyncState

namespace VantaMP::Game {
    using namespace VehicleSyncState;

    void ApplyRemoteVehicleDamage(const Shared::RPC::VehicleDamageReport &report) {
        namespace Field = Shared::RPC::Vehicle::DamageField;

        const auto found = gVehicles.find(report.vehicleId);

        if (found == gVehicles.end() || !found->second.handle) {
            return;
        }

        auto &remote = found->second;

        // SEEDED FROM WHAT WE ALREADY HAVE, NOT FROM ZERO.
        //
        // This used to build a blank VehicleEntity and copy every group out
        // of the packet into it, which meant a report about one panel arrived
        // at ApplyDamageToHandle claiming every door, wheel and light was
        // pristine. It survived only because the old sender always filled all
        // seven groups - so the mask is not just a bandwidth saving here, it
        // is what makes a partial report expressible at all.
        Shared::VehicleEntity entity;

        entity.damageEngine     = remote.lastEngine;
        entity.doorsPacked      = remote.lastDoors;
        entity.wheelsPacked     = remote.lastWheels;
        entity.panels           = remote.lastPanels;
        entity.lights           = remote.lastLights;
        entity.doorRatiosPacked = remote.lastRatios;
        entity.health           = remote.health;

        if (report.Has(Field::kEngine)) {
            entity.damageEngine = report.damageEngine;
        }

        if (report.Has(Field::kDoors)) {
            entity.doorsPacked = report.doorsPacked;
        }

        if (report.Has(Field::kWheels)) {
            entity.wheelsPacked = report.wheelsPacked;
        }

        if (report.Has(Field::kPanels)) {
            entity.panels = report.panels;
        }

        if (report.Has(Field::kLights)) {
            entity.lights = report.lights;
        }

        if (report.Has(Field::kHealth)) {
            entity.health = report.health;
        }

        ApplyDamageToHandle(remote.handle, entity, remote, entity.health > SDK::kWreckHealth);

        const auto local = SDK::ReadVehicle(remote.handle);

        if (report.Has(Field::kHealth) && report.health + 0.5f < local.health) {
            remote.health = report.health;

            SDK::ApplyVehicleStates(remote.handle, report.health, local.engineOn, local.sirenOn);
        }

        if (!remote.wrecked && entity.health <= SDK::kWreckHealth) {
            SDK::WreckVehicle(remote.handle, false);
            remote.wrecked = true;
        }

        if (remote.wrecked) {
            SDK::KeepScriptVehicle(remote.handle);
        }

        // Absent ratios mean DO NOT TOUCH THE DOORS - not "close them".
        if (report.Has(Field::kDoorRatios)) {
            remote.lastRatios       = report.doorRatiosPacked;
            remote.lastEntityRatios = report.doorRatiosPacked;
            BeginDoorInterp(remote, remote.handle, report.doorRatiosPacked);
        }
    }
} // namespace VantaMP::Game
