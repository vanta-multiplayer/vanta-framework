#pragma once

#include "shared/rpc/world_env.h"

namespace VantaMP::Game {
    void ApplyWorldEnv(const Shared::RPC::WorldEnv &env);
    void PulseWorldEnv();
} // namespace VantaMP::Game
