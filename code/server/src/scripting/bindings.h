#pragma once

#include <cstdint>

namespace Framework::Scripting {
    class Engine;
}

namespace VantaMP::Game {
    class PlayerManager;
    class VehicleManager;
}

namespace VantaMP::Scripting {
    // Player.spawn / Player.nickname, hung on the framework Player the
    // scripts already receive. Called from ModuleRegister while the isolate
    // is entered.
    void Register(Framework::Scripting::Engine *engine, Game::PlayerManager &players, Game::VehicleManager &vehicles);

    // Hands a connected player to resources. True when someone is listening,
    // in which case C++ must not also spawn them - that is the resource's job.
    [[nodiscard]] bool EmitPlayerReady(std::uint64_t networkId);

    // Same idea for a death. True when a resource is listening for
    // playerDeath, in which case it is that resource's job to spawn them.
    [[nodiscard]] bool EmitPlayerDeath(std::uint64_t networkId, std::uint64_t killerId, std::uint32_t weaponType);
} // namespace VantaMP::Scripting
