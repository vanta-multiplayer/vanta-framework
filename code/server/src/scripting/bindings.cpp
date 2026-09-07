#include "bindings.h"

#include "scripting/player.h"
#include "scripting/vehicle.h"

#include <scripting/engine.h>

namespace VantaMP::Scripting {
    void Register(Framework::Scripting::Engine *engine, Game::PlayerManager &players, Game::VehicleManager &vehicles) {
        if (!engine) {
            return;
        }

        // InitFrameworkSDK calls this with no V8 scopes of its own. Creating
        // or extending a v8pp class without a locker and a handle scope is
        // an access violation inside libnode - isolate TLS is empty, and the
        // first heap read lands at a low offset (0x1f0 on this crash).
        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Context::Scope contextScope(engine->GetContext());

        auto global = engine->GetContext()->Global();

        Player::Bind(players);
        Player::Register(isolate, global);

        Vehicle::Bind(vehicles);
        Vehicle::Register(isolate, global);
    }

    bool EmitPlayerReady(std::uint64_t networkId) {
        return Player::EmitReady(networkId);
    }

    bool EmitPlayerDeath(std::uint64_t networkId, std::uint64_t killerId, std::uint32_t weaponType) {
        return Player::EmitDeath(networkId, killerId, weaponType);
    }

    bool EmitVehicleStartEnter(std::uint64_t playerId, std::uint64_t vehicleId, std::uint8_t seat, bool jacked, std::uint8_t door) {
        return Vehicle::EmitStartEnter(playerId, vehicleId, seat, jacked, door);
    }

    void EmitVehicleEnter(std::uint64_t playerId, std::uint64_t vehicleId, std::uint8_t seat) {
        Vehicle::EmitEnter(playerId, vehicleId, seat);
    }

    bool EmitVehicleStartExit(std::uint64_t playerId, std::uint64_t vehicleId, std::uint8_t seat, std::uint8_t door) {
        return Vehicle::EmitStartExit(playerId, vehicleId, seat, door);
    }

    void EmitVehicleExit(std::uint64_t playerId, std::uint64_t vehicleId, std::uint8_t seat) {
        Vehicle::EmitExit(playerId, vehicleId, seat);
    }
} // namespace VantaMP::Scripting
