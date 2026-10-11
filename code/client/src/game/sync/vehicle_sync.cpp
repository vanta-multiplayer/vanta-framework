#include "game/sync/vehicle_sync.h"

#include "game/sync/player_sync.h"
#include "game/sync/vehicle/handler.h"
#include "game/sync/vehicle/state.h"
#include "sdk/game.h"
#include "sdk/input/input.h"
#include "sdk/world/streaming.h"
#include "shared/entity/player_entity.h"
#include "shared/world/world_space.h"
#include "shared/rpc/player/enter_vehicle.h"
#include "shared/rpc/player/exit_vehicle.h"
#include "shared/rpc/player/occupancy_abort.h"
#include "shared/rpc/vehicle/damage.h"
#include "shared/rpc/vehicle/driver_sync.h"
#include "shared/rpc/vehicle/unoccupied_sync.h"
#include "shared/rpc/common/metrics.h"
#include "shared/rpc/common/wire_registry.h"
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
    using namespace VehicleSyncState;

    namespace {
        namespace Replication = Framework::Networking::Replication;


        // First frame of a walk-in the enter task still has door 0, which is
        // also FRONT_LEFT. Waiting one tick lets CreateFirstSubTask write the
        // door the owner actually walked to, so remotes shuffle when that
        // door is the passenger side.
        bool gAwaitEnterDoor = false;

        constexpr float kAbortStick = 48.0f;

        [[nodiscard]] float StickMagnitude(const SDK::ControllerInput &input) {
            const auto x = static_cast<float>(input.leftStickX);
            const auto y = static_cast<float>(input.leftStickY);

            return std::sqrt(x * x + y * y);
        }

        [[nodiscard]] bool OccupancyInputIdle() {
            const auto input = SDK::ReadLocalInput();

            return (input.buttons & SDK::Button::EnterExit) == 0 && (input.buttons & SDK::Button::Jump) == 0 && StickMagnitude(input) <= kAbortStick;
        }

        // ONLY REACHED AFTER THE SERVER REFUSED AN ENTRY OR EXIT.
        //
        // The local cancel path no longer aborts anything - see the Entering
        // case below - so the only thing that sets these flags now is
        // ApplyOccupancyAbort, i.e. the server saying "you may not have that
        // seat". Re-issuing the abort while the player keeps the key held is
        // correct there: GTA will happily start a fresh enter task the moment
        // the old one dies, and the server has already said no.
        //
        // SDK::AbortVehicleEnter latches on the task pointer, so a task that
        // is already unwinding is not asked twice; a genuinely new task -
        // which is exactly what this is guarding against - still gets one
        // abort of its own.
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

        void SendLocalExit(bool warp, std::uint8_t door, bool abortEnter = false, void *vehicle = nullptr, bool knockedOff = false) {
            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            // MTA writes GetDoorOpenRatio(enteringDoor) into the notify-abort
            // (CClientPed.cpp:10443) so the receiver has an authoritative
            // angle for a door nobody else can observe. Same idea here: this
            // is the angle at the moment the entry ended. The settle window in
            // ReportOccupantDoorRatios then reports the swing that follows.
            std::uint8_t ratio = 0;

            if (abortEnter && vehicle) {
                const auto index = SDK::DoorRatioIndexForTarget(door);

                if (index != SDK::kInvalidDoor) {
                    const auto state = SDK::ReadVehicle(vehicle);

                    ratio = static_cast<std::uint8_t>((state.doorRatiosPacked >> (index * 8)) & 0xFFu);
                }
            }

            Shared::RPC::PlayerExitVehicle exit;
            exit.FromParameters(0, warp, door, abortEnter, ratio, knockedOff);
            net->BroadcastRPC(exit);

            Framework::Logging::GetLogger("Vehicle")->debug("local exit door {} warp {} abortEnter {}", door, warp, abortEnter);
        }

        // Nothing under us yet: watch for the moment GTA seats us or starts an entry walk.
        void WatchOnFoot(const SDK::PedOccupancy &occupancy, std::uint64_t vehicleId) {
            if (gBlockEnterUntilRelease) {
                gAwaitEnterDoor = false;
                return;
            }

            if (occupancy.seated && vehicleId != 0) {
                gAwaitEnterDoor = false;
                SendLocalEnter(vehicleId, occupancy.seat, true, occupancy.door);
                gLocalRide = LocalRide::Seated;
            }
            else if (occupancy.entering && vehicleId != 0) {
                if (!gAwaitEnterDoor) {
                    gAwaitEnterDoor = true;
                    return;
                }

                gAwaitEnterDoor = false;
                SendLocalEnter(vehicleId, occupancy.seat, false, occupancy.door);
                gLocalRide = LocalRide::Entering;
            }
            else {
                gAwaitEnterDoor = false;
            }
        }

        // An entry walk is running. It ends in a seat, or it is abandoned.
        void WatchEntering(const SDK::PedOccupancy &occupancy, std::uint64_t vehicleId) {
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
                // THE ONLY WAY AN ENTRY ENDS. WE OBSERVE IT, WE DO NOT
                // CAUSE IT.
                //
                // There used to be a second branch here that watched for
                // jump / stick input, called AbortVehicleEnter and forced
                // the state to OnFoot. That was a duplicate of something
                // GTA already does: CPlayerPed's enter task ends itself
                // when the player jumps or steers away - that is what
                // bHasDisplayedPlayerQuitEnterCarHelpText in the save
                // structure is the help text FOR. So on the cancel frame
                // two mechanisms tore at the same task: GTA unwinding it
                // normally, and our MakeAbortable restarting the unwind.
                // That is the glitching, and no amount of latching the
                // abort fixes it, because the abort should not be there.
                //
                // MTA does exactly what is left here. It never intercepts
                // the cancel key at all - there is no abort call anywhere
                // in its local enter path. It polls IsEnteringVehicle()
                // (CClientPed.cpp:8272) and, the frame the task is gone,
                // asks GetRealOccupiedVehicle() whether the ped ended up
                // in a seat or on the pavement (10336-10420). Seat means
                // notify-in; no seat means notify-abort. The cancel is
                // GTA's business; the network's business is reporting
                // which of the two happened.
                SendLocalExit(false, occupancy.door, true, occupancy.vehicle);
                gLocalRide = LocalRide::OnFoot;
            }
        }

        // We are in a seat. Watch for the leave task or for GTA ejecting us outright.
        void WatchSeated(const SDK::PedOccupancy &occupancy, std::uint64_t vehicleId) {
            if (gBlockExitUntilRelease) {
                return;
            }

            if (occupancy.leaving) {
                SendLocalExit(false, occupancy.door);
                gLocalRide = LocalRide::Leaving;
            }
            else if (!occupancy.seated && !occupancy.entering) {
                // THROWN OUT, NOT WALKED OUT.
                //
                // Seated one frame, on the pavement the next, with no
                // leave task in between. Nobody asked for this - GTA did
                // it. A crash on a bike raises CEventKnockOffBike
                // (World.cpp:2008 constructs one for explosions;
                // CEventHandler::ComputeKnockOffBikeResponse is the
                // handler MTA hooks at 0x4BA06F), and the ped is ejected
                // without ever running CTaskComplexLeaveCar.
                //
                // This used to send warp=false, the same as a deliberate
                // exit, which was wrong twice over. The server fires
                // vehicleStartExit for a non-warp exit, so a script could
                // "veto" something that had already physically happened.
                // And the receiver built a CTaskComplexLeaveCar - a calm,
                // open-the-door-and-step-out animation - for a ped who was
                // just launched over the handlebars. That is why the
                // knock-off never looked right on the other machine.
                //
                // warp=true says "they are already out, put them there".
                // It is the same distinction MTA draws between
                // VEHICLE_NOTIFY_OUT and a requested exit.
                SendLocalExit(true, occupancy.door, false, nullptr, true);
                gLocalRide = LocalRide::OnFoot;
            }
        }

        // A leave task is running. It ends on foot.
        void WatchLeaving(const SDK::PedOccupancy &occupancy, std::uint64_t vehicleId) {
            if (!occupancy.seated && !occupancy.leaving) {
                // The leave finished normally - the exit RPC went out when
                // the task started, so there is nothing more to send.
                gLocalRide = LocalRide::OnFoot;
            }
            else if (occupancy.seated && !occupancy.leaving) {
                if (vehicleId != 0) {
                    SendLocalEnter(vehicleId, occupancy.seat, true, occupancy.door);
                }

                gLocalRide = LocalRide::Seated;
            }
        }

        // The local player's own enter/exit state machine.
        //
        // GTA owns the tasks; this only watches what they did and tells the
        // server. Each state is a small handler below - the transitions are the
        // whole point of this function, so they stay visible here.
        void WatchLocalOccupancy() {
            const auto occupancy = SDK::QueryPedOccupancy(0);
            const auto vehicleId = VehicleNetworkIdOf(occupancy.vehicle);

            PulseOccupancyBlocks(occupancy);

            switch (gLocalRide) {
            case LocalRide::OnFoot:
                WatchOnFoot(occupancy, vehicleId);
                break;

            case LocalRide::Entering:
                WatchEntering(occupancy, vehicleId);
                break;

            case LocalRide::Seated:
                WatchSeated(occupancy, vehicleId);
                break;

            case LocalRide::Leaving:
                WatchLeaving(occupancy, vehicleId);
                break;
            }
        }
















        // COMMON BOOKKEEPING, THEN DISPATCH.
        //
        // Everything here applies to every vehicle regardless of its GTA
        // class: cosmetics, damage, doors, wreck state, ownership. The moment
        // physical state is involved - pose, momentum, controls - the frame
        // goes to the handler for that vehicle's type. See
        // game/sync/vehicle/handler.h.
        // Builds the per-frame view a type handler works from, then dispatches to
        // it (BikeSync, AutomobileSync, ...).
        //
        // Cross-frame state lives in RemoteVehicle and is copied back out here:
        // the Frame is deliberately NOT the wire format, so a CBike and a
        // CAutomobile can apply the same packet under different physical policies
        // without either knowing anything about serialization.
        void DispatchToTypeHandler(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            // "Is this a new packet?" is answered by comparing what we are
            // about to aim at against what we last aimed at - the same test
            // the caller used to make inline.
            const std::uint32_t now     = GetTickCount();
            const bool          changed = !remote.aimed || remote.aimedPosition != entity->position || remote.aimedRotation != entity->rotation;

            VehicleSync::Frame frame;

            frame.handle       = remote.handle;
            frame.entity       = entity;
            frame.interpolator = &remote.interpolator;
            frame.aimedPosition = remote.aimedPosition;
            frame.aimedRotation = remote.aimedRotation;
            frame.aimed         = remote.aimed;
            frame.speed         = remote.speed;
            frame.velocity      = remote.velocity;
            frame.angular       = remote.angular;
            frame.packetArrived = changed;
            frame.packetMs      = remote.whenMs;
            frame.hasDriver     = SDK::VehicleHasDriver(remote.handle);

            const auto &handler = VehicleSync::HandlerFor(remote.handle);

            if (changed) {
                remote.velocity = entity->velocity;
                remote.angular  = entity->angularVelocity;

                frame.velocity = remote.velocity;
                frame.angular  = remote.angular;

                if (handler.OnSnapshot) {
                    handler.OnSnapshot(frame);
                }

                remote.aimedPosition = entity->position;
                remote.aimedRotation = entity->rotation;
                remote.speed         = glm::length(entity->velocity);
                remote.whenMs        = now;
                remote.aimed         = true;

                frame.aimedPosition = remote.aimedPosition;
                frame.aimedRotation = remote.aimedRotation;
                frame.aimed         = remote.aimed;
                frame.speed         = remote.speed;
                frame.packetMs      = remote.whenMs;
            }

            if (handler.Apply) {
                handler.Apply(frame);
            }
        }

        void ApplyRemoteVehicle(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
            if (!remote.handle || SDK::IsLocalPlayerDriving(remote.handle)) {
                return;
            }

            // BEFORE GTA PROCESSES IT THIS FRAME. Status decides whether the
            // game runs a vehicle's player-driving path at all - and for a
            // bike that path is what applies steer and lean and clears the
            // handbrake CVehicle's constructor leaves on.
            SDK::SyncVehicleSimulationStatus(remote.handle);

            SDK::SetVehicleInterior(remote.handle, entity->interior);
            ApplyCosmetics(entity, remote);
            ApplyScriptState(entity, remote);
            ApplyExtras(entity, remote);

            remote.health      = entity->health;
            remote.engineOn    = entity->engineOn;
            remote.sirenOn     = entity->sirenOn;
            remote.steer       = entity->steer;
            remote.accelerator = entity->accelerator;
            remote.brake       = entity->brake;
            remote.handbrake   = entity->handbrake;
            remote.gasPedal    = entity->gasPedal;
            remote.brakePedal  = entity->brakePedal;

            const bool partsChanged = !remote.haveDamage || remote.lastEngine != entity->damageEngine || remote.lastDoors != entity->doorsPacked ||
                remote.lastWheels != entity->wheelsPacked || remote.lastPanels != entity->panels || remote.lastLights != entity->lights;

            if (partsChanged && !remote.wrecked) {
                // One flying component per newly-missing part, not a
                // SetupDamageAfterLoad of the whole wreck.
                ApplyDamageToHandle(remote.handle, *entity, remote, entity->health > SDK::kWreckHealth);
            }

            if (!remote.wrecked && (entity->IsBlown() || SDK::IsVehicleWrecked(remote.handle))) {
                SDK::WreckVehicle(remote.handle, false);
                remote.wrecked = true;
            }

            if (remote.wrecked) {
                // GTA deletes a wreck after 60 seconds and will stream the
                // hull out if BlowUpCar dropped the mission lock. Leave it
                // where the boom put it - interpolating a wreck is how it
                // falls through the map.
                SDK::KeepScriptVehicle(remote.handle);
                return;
            }

            if (entity->doorRatiosPacked != remote.lastEntityRatios) {
                remote.lastEntityRatios = entity->doorRatiosPacked;
                remote.lastRatios       = entity->doorRatiosPacked;
                BeginDoorInterp(remote, remote.handle, entity->doorRatiosPacked);
            }

            DispatchToTypeHandler(entity, remote);

            PulseRemoteDoorRatios(remote.handle, remote);
        }


    } // namespace

    void UpdateVehicleSync() {
        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication) {
            return;
        }

        // REUSED, NOT REBUILT. This runs every frame and used to allocate a
        // fresh hash set each time purely to answer "did I see this entity
        // this tick". Clearing keeps the buckets, so after the first few
        // frames it stops touching the allocator entirely.
        static std::unordered_set<std::uint64_t> seen;

        seen.clear();

        replication->ForEachEntity([](Replication::NetworkEntity *entity) {
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

            // The wire id and the sequence die with the vehicle, exactly as
            // on the player path: a mapping left behind would let a motion
            // packet still in flight resolve to a car that no longer exists.
            gWireIds.Release(it->first);

            DestroyHandle(it->second);
            it = gVehicles.erase(it);
        }

        WatchLocalOccupancy();

        // After the per-vehicle pass, so every syncer's GTA state has been
        // read this frame. Rate-limits itself to kUnoccupiedIntervalMs.
        SendUnoccupiedBatches();
    }

    void ClearRemoteVehicles() {
        for (auto &[id, remote] : gVehicles) {
            DestroyHandle(remote);
        }

        gVehicles.clear();
        gVehicleIdByHandle.clear();
        gLateJoinWarp.clear();
        gLocalRide = LocalRide::OnFoot;

        // A disconnect invalidates every mapping: the next server mints its
        // own id space.
        gWireIds.Clear();
        gLastUnoccupiedSendMs = 0;
    }

    std::uint64_t VehicleNetworkIdOf(const void *vehicle) {
        if (!vehicle) {
            return 0;
        }

        const auto found = gVehicleIdByHandle.find(vehicle);

        return found == gVehicleIdByHandle.end() ? 0 : found->second;
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
