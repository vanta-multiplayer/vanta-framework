#include "game/sync/vehicle_sync.h"

#include "game/sync/player_sync.h"
#include "game/sync/vehicle/state.h"
#include "sdk/game.h"
#include "shared/entity/player_entity.h"
#include "shared/rpc/player/occupancy_abort.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>

// SEATING AND UNSEATING REMOTE PLAYERS.
//
// The reliable occupancy RPCs land here. Enter, exit, jack and abort are
// semantic events that play once - they are deliberately NOT derived from the
// motion stream, because a flag that says "entering" would restart the door
// animation on every packet.
//
// Split out of vehicle_sync.cpp, which was doing this alongside four other
// jobs. Shared registry state lives in vehicle/state.h.
// MTA's stream-in warp: a ped that appeared already in a seat, remembered
// until the car exists and they can be put in it. Defined here because this is
// the only file that seats anyone.
namespace VantaMP::Game::VehicleSyncState {
    void RememberLateJoin(int slot) {
        gLateJoinWarp[slot] = true;
    }

    void ForgetLateJoin(int slot) {
        gLateJoinWarp.erase(slot);
    }

    bool ConsumeLateJoin(int slot) {
        const auto found = gLateJoinWarp.find(slot);

        return found != gLateJoinWarp.end() && found->second;
    }
} // namespace VantaMP::Game::VehicleSyncState

namespace VantaMP::Game {
    using namespace VehicleSyncState;

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

        // STILL GETTING UP. A warp into the seat here flushes the tasks
        // (EnterVehicle -> PrepareForTransition) and the stand-up animation
        // vanishes mid-play. The owner will still be reporting the seat next
        // packet, so this costs a tick, not the seat.
        if (SDK::PedIsFallingOffBike(slot)) {
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

            // The seat is what makes this a driven vehicle, so the status
            // that follows from it is settled here rather than a frame later.
            SDK::SyncVehicleSimulationStatus(handle);
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

    void ApplyRemoteExitVehicle(std::uint64_t networkId, bool warp, std::uint8_t door, bool abortEnter, std::uint8_t doorRatio, bool knockedOff) {
        if (networkId == 0 || networkId == LocalNetworkId()) {
            // OUR OWN EXIT COMES BACK TO US - IGNORE IT IF WE ARE ALREADY OUT.
            //
            // The server rebroadcasts to everyone including the sender, and a
            // knock-off now reports warp=true (the ped was ejected, not
            // walked out). Without this check that echo would re-run
            // ExitVehicle on a ped who is already lying on the road, which
            // re-issues CTaskSimpleCarSetPedOut and stamps them back to the
            // door position.
            //
            // The branch still matters for a genuine SERVER-forced warp-out,
            // where we are seated and are being told to get out.
            if (networkId == LocalNetworkId() && warp && SDK::IsPedInVehicle(0)) {
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

        // THE DOOR THEY LEFT BEHIND.
        //
        // An aborted entry is the one case where the door's angle cannot be
        // derived locally: the remote ped stops using it, so our occupancy
        // mask stops protecting it, but the owner is the only one who knows
        // how far it swung. They send it with the abort; we force it, exactly
        // as MTA does on notify-abort (CPacketHandler.cpp:1972-1974).
        if (abortEnter) {
            const auto index = SDK::DoorRatioIndexForTarget(door);

            if (index != SDK::kInvalidDoor) {
                // Read BEFORE ExitVehicle below, while the ped's enter task
                // still names the vehicle it was climbing into.
                const auto occupancy = SDK::QueryPedOccupancy(slot);

                if (occupancy.vehicle) {
                    SDK::ForceVehicleDoorRatio(occupancy.vehicle, index, doorRatio);
                }
            }
        }

        SDK::ExitVehicle(slot, warp, door, knockedOff);

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
} // namespace VantaMP::Game
