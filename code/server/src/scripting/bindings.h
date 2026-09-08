#pragma once

#include <cstdint>

namespace Framework::Scripting {
    class Engine;
}

namespace VantaMP::Game {
    class PlayerManager;
    class VehicleManager;
    class WorldManager;
}

namespace VantaMP::Scripting {
    // Enters the isolate and publishes Player / Vehicle / World. Called from
    // ModuleRegister. Player is the Framework builtin (kick, emit, ping)
    // with spawn and world-space hung on it; Vehicle is an Entity; World is
    // the server clock and env switches.
    void Register(Framework::Scripting::Engine *engine, Game::PlayerManager &players, Game::VehicleManager &vehicles,
                  Game::WorldManager &world);

    [[nodiscard]] bool EmitPlayerReady(std::uint64_t networkId);
    [[nodiscard]] bool EmitPlayerDeath(std::uint64_t networkId, std::uint64_t killerId, std::uint32_t weaponType);

    // Return false vetoes the climb or the leave. No listeners means allow.
    [[nodiscard]] bool EmitVehicleStartEnter(std::uint64_t playerId, std::uint64_t vehicleId, std::uint8_t seat, bool jacked, std::uint8_t door);
    void EmitVehicleEnter(std::uint64_t playerId, std::uint64_t vehicleId, std::uint8_t seat);
    [[nodiscard]] bool EmitVehicleStartExit(std::uint64_t playerId, std::uint64_t vehicleId, std::uint8_t seat, std::uint8_t door);
    void EmitVehicleExit(std::uint64_t playerId, std::uint64_t vehicleId, std::uint8_t seat);
} // namespace VantaMP::Scripting
