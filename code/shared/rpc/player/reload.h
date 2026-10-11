#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // A player started a reload. Same reason as PlayerShot: it is an event,
    // not a field the snapshot can carry without either missing it or
    // restarting the animation every tick.
    struct PlayerReload {
        static constexpr const char *kIdentifier = "VantaMP::PlayerReload";

        std::uint64_t networkId = 0;

        void FromParameters(std::uint64_t id) {
            networkId = id;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, networkId);
        }
    };
} // namespace VantaMP::Shared::RPC
