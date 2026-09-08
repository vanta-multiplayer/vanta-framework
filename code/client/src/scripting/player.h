#pragma once

#include <scripting/builtins/player.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <v8.h>
#include <v8pp/class.hpp>

namespace VantaMP::Shared {
    class PlayerEntity;
}

namespace VantaMP::Scripting {
    // Client-side player handle. Framework Player (Entity pose + nametag
    // getters) plus readonly Vanta replica fields. No spawn / kick / script
    // setters — those stay server-authoritative.
    class Player final: public Framework::Scripting::Builtins::Player {
      public:
        explicit Player(std::uint64_t networkId);

        Shared::PlayerEntity *ResolvePlayer() const;

        std::string GetNickname() const;
        float GetHealth() const;
        float GetArmour() const;
        std::uint32_t GetModel() const;
        std::int32_t GetMoney() const;
        std::uint8_t GetWantedLevel() const;
        bool GetFrozen() const;
        bool GetCollisions() const;
        std::uint8_t GetFightingStyle() const;
        float GetOxygen() const;
        std::uint16_t GetWalkingStyle() const;
        std::uint8_t GetAlpha() const;
        bool GetOnFire() const;
        float GetMaxHealth() const;
        bool GetJetPack() const;
        bool IsDrowning() const;
        std::uint32_t GetWeapon() const;
        std::uint32_t GetAmmo() const;
        std::uint32_t GetAmmoInClip() const;
        bool IsDead() const;
        bool IsDucked() const;
        bool IsInWater() const;
        bool IsInAir() const;
        bool IsInVehicle() const;
        std::uint8_t GetVehicleSeat() const;
        std::uint8_t GetInterior() const;
        std::uint16_t GetDimension() const;

        static v8pp::class_<Player> &GetClass(v8::Isolate *isolate);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8::Local<v8::Object> Wrap(v8::Isolate *isolate, std::uint64_t networkId);
        // Null before the local ped has a network id.
        static v8::Local<v8::Value> CurrentLocal(v8::Isolate *isolate);

      private:
        static void JS_GetLocal(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_LocalPlayerGetter(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value> &info);
        static void JS_GetOccupiedVehicle(const v8::FunctionCallbackInfo<v8::Value> &info);

        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Player>>> gClasses;
    };
} // namespace VantaMP::Scripting
