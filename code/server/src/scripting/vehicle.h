#pragma once

#include <scripting/builtins/color.h>
#include <scripting/builtins/entity.h>
#include <scripting/builtins/vector3.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <v8.h>
#include <v8pp/class.hpp>

namespace VantaMP::Game {
    class VehicleManager;
}

namespace VantaMP::Shared {
    class VehicleEntity;
}

namespace VantaMP::Scripting {
    // Script handle for a VehicleEntity. Position is the Framework Entity
    // one (ForceState). Rotation is MTA degrees X, Y, Z. Colours are
    // Framework Color — any RGB, not a GTA palette index.
    class Vehicle final: public Framework::Scripting::Builtins::Entity {
      public:
        explicit Vehicle(std::uint64_t networkId);

        Shared::VehicleEntity *ResolveVehicle() const;

        std::uint32_t GetModel() const;

        Framework::Scripting::Builtins::Color GetColor1() const;
        void SetColor1(const Framework::Scripting::Builtins::Color &color);

        Framework::Scripting::Builtins::Color GetColor2() const;
        void SetColor2(const Framework::Scripting::Builtins::Color &color);

        Framework::Scripting::Builtins::Color GetColor3() const;
        void SetColor3(const Framework::Scripting::Builtins::Color &color);

        Framework::Scripting::Builtins::Color GetColor4() const;
        void SetColor4(const Framework::Scripting::Builtins::Color &color);

        std::uint8_t GetPaintjob() const;
        void SetPaintjob(std::uint8_t paintjob);

        std::uint8_t GetInterior() const;
        void SetInterior(std::uint8_t interior);

        std::uint16_t GetDimension() const;
        void SetDimension(std::uint16_t dimension);

        bool AddUpgrade(std::uint16_t upgradeId);
        bool RemoveUpgrade(std::uint16_t upgradeId);
        void RemoveAllUpgrades();

        float GetHealth() const;
        void SetHealth(float health);

        bool GetEngine() const;
        void SetEngine(bool on);

        bool GetSiren() const;
        void SetSiren(bool on);

        bool GetLocked() const;
        void SetLocked(bool locked);

        std::uint8_t GetOverrideLights() const;
        void SetOverrideLights(std::uint8_t mode);

        Framework::Scripting::Builtins::Vector3 GetVelocity() const;
        void SetVelocity(const Framework::Scripting::Builtins::Vector3 &velocity);

        Framework::Scripting::Builtins::Vector3 GetTurnVelocity() const;
        void SetTurnVelocity(const Framework::Scripting::Builtins::Vector3 &velocity);

        // MTA degrees about X, Y, Z — not the Entity quaternion, and not
        // GLM's euler constructor. The replica still stores a quat.
        Framework::Scripting::Builtins::Vector3 GetRotationDegrees() const;
        void SetRotationDegrees(const Framework::Scripting::Builtins::Vector3 &degrees);

        bool GetBlown() const;
        void Fix();
        void Blow();

        std::string GetPlateText() const;
        void SetPlateText(const std::string &text);

        std::uint8_t GetDirtLevel() const;
        void SetDirtLevel(std::uint8_t level);

        bool GetTaxiLight() const;
        void SetTaxiLight(bool on);

        Framework::Scripting::Builtins::Color GetHeadlightColor() const;
        void SetHeadlightColor(const Framework::Scripting::Builtins::Color &color);

        bool GetFrozen() const;
        void SetFrozen(bool frozen);

        bool GetDamageProof() const;
        void SetDamageProof(bool proof);

        bool GetCollisions() const;
        void SetCollisions(bool enabled);

        bool GetLandingGearDown() const;
        void SetLandingGearDown(bool down);

        bool GetLights() const;
        void SetLights(bool on);

        std::uint8_t GetAlpha() const;
        void SetAlpha(std::uint8_t alpha);

        bool IsOnGround() const;

        bool GetEngineBroken() const;
        void SetEngineBroken(bool broken);

        bool GetWheelsCanBurst() const;
        void SetWheelsCanBurst(bool canBurst);

        bool GetCanBeDamaged() const;
        void SetCanBeDamaged(bool can);

        std::uint8_t GetNitroCount() const;
        void SetNitroCount(std::uint8_t count);

        std::string GetName() const;
        std::string GetVehicleType() const;
        std::uint8_t GetMaxPassengers() const;

        std::uint16_t GetUpgradeOnSlot(std::uint8_t slot) const;

        std::uint8_t GetDoorState(std::uint8_t door) const;
        void SetDoorState(std::uint8_t door, std::uint8_t state);

        std::uint8_t GetWheelState(std::uint8_t wheel) const;
        void SetWheelState(std::uint8_t wheel, std::uint8_t state);

        std::uint8_t GetLightState(std::uint8_t light) const;
        void SetLightState(std::uint8_t light, std::uint8_t state);

        std::uint8_t GetPanelState(std::uint8_t panel) const;
        void SetPanelState(std::uint8_t panel, std::uint8_t state);

        float GetDoorOpenRatio(std::uint8_t door) const;
        void SetDoorOpenRatio(std::uint8_t door, float ratio);

        void Destroy();

        static void Bind(Game::VehicleManager &vehicles);
        static v8pp::class_<Vehicle> &GetClass(v8::Isolate *isolate);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static v8::Local<v8::Object> Wrap(v8::Isolate *isolate, std::uint64_t networkId);
        static void Create(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void DestroyById(const v8::FunctionCallbackInfo<v8::Value> &info);

        static bool EmitStartEnter(std::uint64_t playerId, std::uint64_t vehicleId, std::uint8_t seat, bool jacked, std::uint8_t door);
        static void EmitEnter(std::uint64_t playerId, std::uint64_t vehicleId, std::uint8_t seat);
        static bool EmitStartExit(std::uint64_t playerId, std::uint64_t vehicleId, std::uint8_t seat, std::uint8_t door);
        static void EmitExit(std::uint64_t playerId, std::uint64_t vehicleId, std::uint8_t seat);

      private:
        static void JS_SetColor(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetRotation(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetOccupants(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetController(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetUpgrades(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetCompatibleUpgrades(const v8::FunctionCallbackInfo<v8::Value> &info);

        inline static Game::VehicleManager *gVehicles = nullptr;
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Vehicle>>> gClasses;
    };
} // namespace VantaMP::Scripting
