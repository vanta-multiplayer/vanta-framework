#include "player_manager.h"

#include "shared/player_entity.h"
#include "shared/rpc/local_player_spawn.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <world/types/player.h>

namespace VantaMP::Game {
    bool PlayerManager::Add(MafiaNet::PeerGuid guid, const std::string &nickname) {
        const auto logger = Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER);

        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication) {
            return false;
        }

        auto *player = replication->CreateEntity<Shared::PlayerEntity>();

        if (!player) {
            logger->error("Could not create an entity for {}; they will connect to nothing", nickname);
            return false;
        }

        player->nickname = nickname;

        Framework::World::Archetypes::PlayerArchetype archetype;
        archetype.SetupServer(player, guid);

        replication->SetViewer(guid, player);

        _players[guid] = player;

        const std::string name = nickname.empty() ? "A player" : nickname;

        logger->info("{} connected as network id {}", name, static_cast<std::uint64_t>(player->GetNetworkID()));

        return true;
    }

    void PlayerManager::Remove(MafiaNet::PeerGuid guid) {
        const auto found = _players.find(guid);

        if (found == _players.end()) {
            // A peer that never got as far as an entity - a failed handshake, or
            // a disconnect during connect. Nothing to tear down.
            return;
        }

        // Destroying the entity is what tells everyone else this player is gone;
        // there is no separate despawn message.
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->DestroyEntity(found->second);
        }

        _players.erase(found);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("A player disconnected; {} remain", _players.size());
    }

    bool PlayerManager::Spawn(MafiaNet::PeerGuid guid, float x, float y, float z, float heading, std::uint32_t modelId, float health, float armour) {
        auto *player = Find(guid);

        if (!player) {
            return false;
        }

        // The server's own copy first, because that is what the interest grid
        // reads. Sending the client a position it then reports back would work
        // eventually, but leaves a window where nobody can see anybody.
        player->position = glm::vec3(x, y, z);
        player->health   = health;
        player->armour   = armour;

        if (modelId != 0) {
            player->modelId = modelId;
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return false;
        }

        Shared::RPC::LocalPlayerSpawn message;
        message.FromParameters(x, y, z, heading, player->modelId, health, armour);

        net->SendRPC(message, MafiaNet::ToGuid(guid));

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Spawned {} at {:.1f}, {:.1f}, {:.1f}", static_cast<std::uint64_t>(player->GetNetworkID()), x, y, z);

        return true;
    }

    Shared::PlayerEntity *PlayerManager::Find(MafiaNet::PeerGuid guid) const {
        const auto found = _players.find(guid);

        return found == _players.end() ? nullptr : found->second;
    }
} // namespace VantaMP::Game
