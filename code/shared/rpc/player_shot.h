#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // One bullet leaving a player's gun.
    //
    // NOT ON THE SNAPSHOT. Pose, keys and "what they are holding" repeat ten
    // times a second; a shot happens once. MTA puts the same three facts -
    // weapon, origin, target - on PLAYER_BULLETSYNC for that reason. Riding
    // the tick would either miss the shot between updates or invent extras
    // from a held fire button.
    //
    // networkId is the SHOOTER. The client that fired leaves it at zero; the
    // server stamps the connection's entity so a peer cannot impersonate.
    struct PlayerShot {
        static constexpr const char *kIdentifier = "VantaMP::PlayerShot";

        std::uint64_t networkId = 0;
        std::uint32_t weaponType = 0;

        float originX = 0.0f;
        float originY = 0.0f;
        float originZ = 0.0f;

        float targetX = 0.0f;
        float targetY = 0.0f;
        float targetZ = 0.0f;

        std::uint8_t order = 0;

        // Who this bullet hit on the shooter's client, and for how much.
        // MTA carries the same pair on PLAYER_BULLETSYNC: the victim's client
        // is health-authoritative, and interpolation can put them off the
        // ray, so FireInstantHit alone is not enough.
        std::uint64_t damagedNetworkId = 0;
        float         damage           = 0.0f;

        void FromParameters(std::uint64_t id, std::uint32_t weapon, float ox, float oy, float oz, float tx, float ty, float tz, std::uint8_t sequence, std::uint64_t hitId = 0, float hitDamage = 0.0f) {
            networkId         = id;
            weaponType        = weapon;
            originX           = ox;
            originY           = oy;
            originZ           = oz;
            targetX           = tx;
            targetY           = ty;
            targetZ           = tz;
            order             = sequence;
            damagedNetworkId  = hitId;
            damage            = hitDamage;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, networkId);
            bs->Serialize(write, weaponType);
            bs->Serialize(write, originX);
            bs->Serialize(write, originY);
            bs->Serialize(write, originZ);
            bs->Serialize(write, targetX);
            bs->Serialize(write, targetY);
            bs->Serialize(write, targetZ);
            bs->Serialize(write, order);
            bs->Serialize(write, damagedNetworkId);
            bs->Serialize(write, damage);
        }
    };
} // namespace VantaMP::Shared::RPC
