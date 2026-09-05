#pragma once

#include <integrations/server/instance.h>

#include <mafianet/types.h>

namespace VantaMP {
    class Server: public Framework::Integrations::Server::Instance {
      public:
        void PostInit() override;
        void PostUpdate() override;
        void PreShutdown() override;
        void ModuleRegister(Framework::Scripting::Engine *engine) override;

        void OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &data) override;
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid) override;
    };
} // namespace VantaMP
