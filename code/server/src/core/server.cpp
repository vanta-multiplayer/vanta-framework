#include "server.h"

#include "shared/player_entity.h"

#include <cstdint>
#include <string>

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/entity_registry.h>
#include <networking/replication/replication_manager.h>
#include <world/types/player.h>

namespace VantaMP {
    namespace Replication = Framework::Networking::Replication;

    void Server::PostInit() {
        // The type has to exist before any entity of it can be created, and its
        // id - the CRC32 of this name - is what goes on the wire. The client
        // registers the same name, so both sides agree without either shipping
        // a number.
        Replication::EntityRegistry::Get().Register<Shared::PlayerEntity>(Shared::PlayerEntity::kTypeName);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("VantaMP server ready");
    }

    void Server::PostUpdate() {}

    void Server::PreShutdown() {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("VantaMP server shutting down");
    }

    void Server::ModuleRegister(Framework::Scripting::Engine *) {}

    void Server::OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &data) {
        const auto logger = Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER);

        auto *replication = Framework::CoreModules::GetReplication();

        auto *player = replication->CreateEntity<Shared::PlayerEntity>();

        if (!player) {
            logger->error("Could not create an entity for {}; they will connect to nothing", data.nickname);
            return;
        }

        player->nickname = data.nickname;

        // THE AVATAR IS ALSO THE VIEWER. Its position and stream range decide
        // what this connection is told about, so a player who has no entity sees
        // an empty world - and one whose entity is not registered as the viewer
        // sees everything, everywhere, at once.
        Framework::World::Archetypes::PlayerArchetype archetype;
        archetype.SetupServer(player, data.guid);

        replication->SetViewer(data.guid, player);

        _players[data.guid] = player;

        const std::string name = data.nickname.empty() ? "A player" : data.nickname;

        logger->info("{} connected as network id {}", name, static_cast<std::uint64_t>(player->GetNetworkID()));
    }

    void Server::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        const auto logger = Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER);

        const auto found = _players.find(guid);

        if (found == _players.end()) {
            // A peer that never got as far as an entity - a failed handshake, or
            // a disconnect during connect. Nothing to tear down.
            return;
        }

        // Destroying the entity is what tells everyone else this player is gone;
        // there is no separate despawn message.
        Framework::CoreModules::GetReplication()->DestroyEntity(found->second);

        _players.erase(found);

        logger->info("A player disconnected; {} remain", _players.size());
    }
} // namespace VantaMP
