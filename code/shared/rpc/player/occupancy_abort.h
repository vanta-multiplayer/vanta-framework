#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // Server telling everyone — including the player who asked — to stop
    // an enter or leave. Enter/exit RPCs skip the local client so a walk
    // they started is not restarted; this one has to reach them so a
    // script veto or a mid-walk abort actually stands them back up.
    struct PlayerOccupancyAbort {
        static constexpr const char *kIdentifier = "VantaMP::PlayerOccupancyAbort";

        static constexpr std::uint8_t kEnter = 0;
        static constexpr std::uint8_t kExit  = 1;

        std::uint64_t networkId = 0;
        std::uint64_t vehicleId = 0;
        std::uint8_t  kind      = kEnter;
        std::uint8_t  seat      = 0;
        std::uint8_t  door      = 0xFF;

        void FromParameters(std::uint64_t id, std::uint64_t vehicle, std::uint8_t abortKind, std::uint8_t usedSeat = 0, std::uint8_t usedDoor = 0xFF) {
            networkId = id;
            vehicleId = vehicle;
            kind      = abortKind;
            seat      = usedSeat;
            door      = usedDoor;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, networkId);
            bs->Serialize(write, vehicleId);
            bs->Serialize(write, kind);
            bs->Serialize(write, seat);
            bs->Serialize(write, door);
        }
    };
} // namespace VantaMP::Shared::RPC
