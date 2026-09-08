#pragma once

#include "shared/rpc/eac_messages.h"

namespace VantaMP::Eac {
    // Optional Anti-Cheat session on top of Eos::Manager.
    // Requires a protected launch through client\start_protected_game.exe
    // plus a Dev Portal Anti-Cheat client module for this deployment.
    void Init();
    void Shutdown();
    void Tick();

    void OnGameConnected();
    void EndSession();

    void HandleOpaqueFromServer(const Shared::RPC::EacOpaque &msg);
    void HandleActionFromServer(const Shared::RPC::EacAction &msg);

    [[nodiscard]] bool IsActive();
} // namespace VantaMP::Eac
