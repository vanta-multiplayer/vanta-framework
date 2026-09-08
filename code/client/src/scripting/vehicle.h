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

namespace VantaMP::Shared {
    class VehicleEntity;
}

namespace VantaMP::Scripting {
    // Client-side vehicle handle over a VehicleEntity replica. Readonly —
    // create/destroy and script switches live on the server.
    class Vehicle final: public Framework::Scripting::Builtins::Entity {
      public:
        explicit Vehicle(std::uint64_t networkId);

        Shared::VehicleEntity *ResolveVehicle() const;

        std::uint32_t GetModel() const;
        Framework::Scripting::Builtins::Color GetColor1() const;
        Framework::Scripting::Builtins::Color GetColor2() const;
        Framework::Scripting::Builtins::Color GetColor3() const;
        Framework::Scripting::Builtins::Color GetColor4() const;
        std::uint8_t GetPaintjob() const;
        float GetHealth() const;
        bool GetEngine() const;
        bool GetSiren() const;
        bool GetLocked() const;
        bool GetBlown() const;
        std::string GetPlateText() const;
        std::uint8_t GetDirtLevel() const;
        bool GetTaxiLight() const;
        bool GetFrozen() const;
        bool GetDamageProof() const;
        bool GetCollisions() const;
        bool GetLandingGearDown() const;
        bool GetLights() const;
        std::uint8_t GetAlpha() const;
        bool IsOnGround() const;
        bool GetEngineBroken() const;
        std::uint8_t GetInterior() const;
        std::uint16_t GetDimension() const;
        Framework::Scripting::Builtins::Vector3 GetVelocity() const;
        Framework::Scripting::Builtins::Vector3 GetTurnVelocity() const;
        Framework::Scripting::Builtins::Vector3 GetRotationDegrees() const;

        static v8pp::class_<Vehicle> &GetClass(v8::Isolate *isolate);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8::Local<v8::Object> Wrap(v8::Isolate *isolate, std::uint64_t networkId);

      private:
        static void JS_GetController(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetOccupants(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetOccupant(const v8::FunctionCallbackInfo<v8::Value> &info);

        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Vehicle>>> gClasses;
    };
} // namespace VantaMP::Scripting
