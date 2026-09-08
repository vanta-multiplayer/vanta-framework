#pragma once

#include "shared/rpc/eac_messages.h"

#include <mafianet/types.h>

#include <cstdint>
#include <string>

namespace VantaMP {
    class Server;
}

namespace VantaMP::Eac {
    // Create EOS platform + BeginSession when credentials + SDK are present.
    void Init();
    void Shutdown();
    void Tick();

    // Start the register deadline once the player has a network entity.
    void OnPlayerConnected(MafiaNet::PeerGuid guid);
    void OnPlayerDisconnect(MafiaNet::PeerGuid guid);

    void HandleRegister(Server &server, MafiaNet::PeerGuid guid, const Shared::RPC::EacRegister &msg);
    void HandleOpaqueFromClient(MafiaNet::PeerGuid guid, const Shared::RPC::EacOpaque &msg);

    [[nodiscard]] bool IsActive();
} // namespace VantaMP::Eac
