#pragma once

#include <scripting/builtins/player.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <v8.h>
#include <v8pp/class.hpp>

namespace VantaMP::Game {
    class PlayerManager;
}

namespace VantaMP::Shared {
    class PlayerEntity;
}

namespace VantaMP::Scripting {
    // Vanta player handle. This is the Framework Player (kick, emit, ping,
    // nametag, Entity position / rotation) plus spawn, nickname, interior
    // and dimension. playerReady / playerDeath receive this type so a
    // resource already has spawn.
    class Player final: public Framework::Scripting::Builtins::Player {
      public:
        explicit Player(std::uint64_t networkId);

        Shared::PlayerEntity *ResolvePlayer() const;

        std::string GetNickname() const;

        std::uint8_t GetInterior() const;
        void SetInterior(std::uint8_t interior);

        std::uint16_t GetDimension() const;
        void SetDimension(std::uint16_t dimension);

        static void Bind(Game::PlayerManager &players);
        static v8pp::class_<Player> &GetClass(v8::Isolate *isolate);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static v8::Local<v8::Object> Wrap(v8::Isolate *isolate, std::uint64_t networkId);

        static bool EmitReady(std::uint64_t networkId);
        static bool EmitDeath(std::uint64_t networkId, std::uint64_t killerId, std::uint32_t weaponType);

      private:
        static void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info);

        inline static Game::PlayerManager *gPlayers = nullptr;
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Player>>> gClasses;
    };
} // namespace VantaMP::Scripting
