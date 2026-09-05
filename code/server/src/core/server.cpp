#include "server.h"

#include <logging/logger.h>

namespace VantaMP {
    void Server::PostInit() {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("VantaMP server initialized");
    }

    void Server::PostUpdate() {}

    void Server::PreShutdown() {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("VantaMP server shutting down");
    }

    void Server::ModuleRegister(Framework::Scripting::Engine *) {}

    void Server::OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &) {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player connected");
    }

    void Server::OnPlayerDisconnect(MafiaNet::PeerGuid) {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player disconnected");
    }
} // namespace VantaMP
