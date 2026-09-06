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

        void FromParameters(std::uint64_t id, bool instant, std::uint8_t usedDoor = 0xFF) {
            networkId = id;
            warp      = instant;
            door      = usedDoor;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, networkId);
            bs->Serialize(write, warp);
            bs->Serialize(write, door);
        }
    };
} // namespace VantaMP::Shared::RPC
