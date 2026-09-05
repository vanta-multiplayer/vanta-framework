#pragma once

#include <integrations/server/instance.h>

#include <mafianet/types.h>

#include <cstdint>
#include <unordered_map>

namespace VantaMP {
    namespace Shared {
        class PlayerEntity;
    }

    class Server: public Framework::Integrations::Server::Instance {
      public:
        void PostInit() override;
        void PostUpdate() override;
        void PreShutdown() override;
        void ModuleRegister(Framework::Scripting::Engine *engine) override;

        void OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &data) override;
        void OnPlayerReady(MafiaNet::PeerGuid guid) override;
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid) override;

        // Places a player in the world, or moves one already there - which is
        // what a respawn is, so death needs no separate resurrect.
        //
        // THE SERVER DECIDES WHETHER A PLAYER EXISTS IN THE WORLD. A connected
        // player who has not been spawned has no ped: they may be in a menu,
        // spectating, or waiting on a resource. Only the server knows which.
        bool SpawnPlayer(MafiaNet::PeerGuid guid, float x, float y, float z, float heading = 0.0f, std::uint32_t modelId = 0, float health = 100.0f, float armour = 0.0f);

      private:
        // Which entity belongs to which connection, so a disconnect can find
        // what to destroy. The replication manager owns the entities; this only
        // remembers them.
        std::unordered_map<MafiaNet::PeerGuid, Shared::PlayerEntity *> _players;
    };
} // namespace VantaMP
