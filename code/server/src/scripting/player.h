#pragma once

#include <scripting/builtins/player.h>

#include <cstdint>
#include <memory>
#include <string>
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
        void SetNickname(const std::string &nickname);

        float GetHealth() const;
        void SetHealth(float health);

        float GetArmour() const;
        void SetArmour(float armour);

        std::uint32_t GetModel() const;
        void SetModel(std::uint32_t modelId);

        std::int32_t GetMoney() const;
        void SetMoney(std::int32_t money);

        std::uint8_t GetWantedLevel() const;
        void SetWantedLevel(std::uint8_t stars);

        bool GetFrozen() const;
        void SetFrozen(bool frozen);

        bool GetCollisions() const;
        void SetCollisions(bool enabled);

        std::uint8_t GetFightingStyle() const;
        void SetFightingStyle(std::uint8_t style);

        float GetOxygen() const;
        void SetOxygen(float oxygen);

        std::uint16_t GetWalkingStyle() const;
        void SetWalkingStyle(std::uint16_t style);

        std::uint8_t GetAlpha() const;
        void SetAlpha(std::uint8_t alpha);

        bool GetOnFire() const;
        void SetOnFire(bool on);

        float GetMaxHealth() const;
        void SetMaxHealth(float maxHealth);

        bool IsOnGround() const;
        bool IsDrowning() const;

        std::uint32_t GetWeapon() const;
        std::uint32_t GetAmmo() const;
        std::uint32_t GetAmmoInClip() const;
        std::uint8_t GetWeaponSlot() const;
        void SetWeaponSlot(std::uint8_t slot);

        bool IsDead() const;
        bool IsDucked() const;
        bool IsInWater() const;
        bool IsInAir() const;
        bool IsInVehicle() const;
        std::uint8_t GetVehicleSeat() const;

        void GiveWeapon(std::uint32_t weaponType, std::uint32_t ammo);
        void TakeWeapon(std::uint32_t weaponType);
        void TakeAllWeapons();
        void SetWeaponAmmo(std::uint32_t weaponType, std::uint32_t ammo);
        void Kill();

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
        static void JS_GetOccupiedVehicle(const v8::FunctionCallbackInfo<v8::Value> &info);

        inline static Game::PlayerManager *gPlayers = nullptr;
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Player>>> gClasses;
    };
} // namespace VantaMP::Scripting
