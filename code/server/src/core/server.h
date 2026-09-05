#pragma once

#include <integrations/server/instance.h>

#include <mafianet/types.h>

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
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid) override;

      private:
        // Which entity belongs to which connection, so a disconnect can find
        // what to destroy. The replication manager owns the entities; this only
        // remembers them.
        std::unordered_map<MafiaNet::PeerGuid, Shared::PlayerEntity *> _players;
    };
} // namespace VantaMP
