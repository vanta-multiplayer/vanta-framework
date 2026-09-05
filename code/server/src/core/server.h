#pragma once

#include "game/player_manager.h"

#include <integrations/server/instance.h>

#include <mafianet/types.h>

namespace VantaMP {
    // The server's lifecycle, and nothing else.
    //
    // What a player IS lives in Game::PlayerManager. Keeping the two apart is
    // what stops this becoming the file every feature has to be added to.
    class Server: public Framework::Integrations::Server::Instance {
      public:
        void PostInit() override;
        void PostUpdate() override;
        void PreShutdown() override;
        void ModuleRegister(Framework::Scripting::Engine *engine) override;

        void OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &data) override;
        void OnPlayerReady(MafiaNet::PeerGuid guid) override;
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid) override;

        [[nodiscard]] Game::PlayerManager &Players() {
            return _players;
        }

      private:
        Game::PlayerManager _players;
    };
} // namespace VantaMP
