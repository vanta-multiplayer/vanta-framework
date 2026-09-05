#include "server.h"

#include "shared/player_entity.h"
#include "shared/spawn_points.h"

#include <logging/logger.h>
#include <networking/replication/entity_registry.h>

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
        _players.Add(data.guid, data.nickname);
    }

    void Server::OnPlayerReady(MafiaNet::PeerGuid guid) {
        // CONNECTING IS NOT SPAWNING. Until this the player has an entity but no
        // place in the world, and the server's copy of that entity is wherever
        // it was created rather than where anyone is standing.
        //
        // A fixed point for now. This is the seam a resource takes over: it
        // decides whether a player spawns at all, and where.
        _players.Spawn(guid, Shared::Spawn::kDefaultX, Shared::Spawn::kDefaultY, Shared::Spawn::kDefaultZ);
    }

    void Server::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        _players.Remove(guid);
    }
} // namespace VantaMP
