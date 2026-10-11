#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // This client has just died.
    //
    // NOT ON THE SNAPSHOT. Health reaching zero is an event: the server has
    // to hear it once so a resource can respawn, and riding the tick would
    // either miss the frame or fire playerDeath every 100ms while the body
    // is still down. MTA's PLAYER_WASTED packet is the same idea.
    //
    // The client that died leaves networkId at zero; the server stamps the
    // connection so a peer cannot report someone else's death. killerId is
    // whoever last damaged this ped on this client, or zero if they fell
    // in the water on their own.
    struct PlayerDeath {
        static constexpr const char *kIdentifier = "VantaMP::PlayerDeath";

        std::uint64_t networkId = 0;
        std::uint64_t killerId  = 0;
        std::uint32_t weaponType = 0;

        void FromParameters(std::uint64_t id, std::uint64_t killer, std::uint32_t weapon) {
            networkId  = id;
            killerId   = killer;
            weaponType = weapon;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, networkId);
            bs->Serialize(write, killerId);
            bs->Serialize(write, weaponType);
        }
    };
} // namespace VantaMP::Shared::RPC
