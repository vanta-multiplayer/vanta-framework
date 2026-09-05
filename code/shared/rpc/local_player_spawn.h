#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // The server placing THIS player in the world.
    //
    // WHY THE LOCAL PLAYER NEEDS ITS OWN MESSAGE. A replicated PlayerEntity
    // describes somebody else, and a client turns one into a remote ped. This
    // is about the ped the player controls: it already exists, so it has to be
    // MOVED rather than created - and the client must not place it on its own,
    // because where a player belongs is the server's decision.
    //
    // Until this arrives a connected player has no place in the world. They may
    // be in a spawn menu, spectating, or waiting for a resource to decide. MTA
    // works the same way, and its client waits for a resource to call
    // spawnPlayer.
    //
    // IT IS ALSO WHAT MAKES OTHER PLAYERS VISIBLE. The server's copy of a
    // player's entity is what the interest grid measures from, and until the
    // server has decided where somebody is, that copy is wherever it was
    // created. Two players spawning themselves client-side are three kilometres
    // from where the server believes they are, inside a hundred-unit radius -
    // so each is told about nobody, and the symptom is an empty world rather
    // than anything that points at spawning.
    struct LocalPlayerSpawn {
        static constexpr const char *kIdentifier = "VantaMP::LocalPlayerSpawn";

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        float heading = 0.0f;

        std::uint32_t modelId = 0;

        float health = 100.0f;
        float armour = 0.0f;

        void FromParameters(float posX, float posY, float posZ, float facing, std::uint32_t model, float hp, float armor) {
            x       = posX;
            y       = posY;
            z       = posZ;
            heading = facing;
            modelId = model;
            health  = hp;
            armour  = armor;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, x);
            bs->Serialize(write, y);
            bs->Serialize(write, z);
            bs->Serialize(write, heading);
            bs->Serialize(write, modelId);
            bs->Serialize(write, health);
            bs->Serialize(write, armour);
        }
    };
} // namespace VantaMP::Shared::RPC
