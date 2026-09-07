#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // A player is getting out of the vehicle they occupy.
    //
    // Carries no vehicle id: a player occupies at most one, and both sides
    // already know which. Sending it would create a second source of truth
    // that could disagree with the first.
    struct PlayerExitVehicle {
        static constexpr const char *kIdentifier = "VantaMP::PlayerExitVehicle";

        std::uint64_t networkId = 0;
        bool          warp      = false;
        // Same GTA door the owner opened. 0xFF lets the receiver compute it.
        std::uint8_t door = 0xFF;
        // Walk-away / denied climb, not a leave. The server must not fire
        // vehicleStartExit or a resource can trap them mid-walk.
        bool abortEnter = false;

        void FromParameters(std::uint64_t id, bool instant, std::uint8_t usedDoor = 0xFF, bool cancelEnter = false) {
            networkId  = id;
            warp       = instant;
            door       = usedDoor;
            abortEnter = cancelEnter;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, networkId);
            bs->Serialize(write, warp);
            bs->Serialize(write, door);
            bs->Serialize(write, abortEnter);
        }
    };
} // namespace VantaMP::Shared::RPC
