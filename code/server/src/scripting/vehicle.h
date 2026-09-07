#pragma once

#include <scripting/builtins/color.h>
#include <scripting/builtins/entity.h>

#include <cstdint>
#include <memory>
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
    // Script handle for a VehicleEntity. Position and rotation are the
    // Framework Entity ones (ForceState). Colours are Framework Color —
    // any RGB, not a GTA palette index.
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

        void Destroy();

        static void Bind(Game::VehicleManager &vehicles);
        static v8pp::class_<Vehicle> &GetClass(v8::Isolate *isolate);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static v8::Local<v8::Object> Wrap(v8::Isolate *isolate, std::uint64_t networkId);
        static void Create(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void DestroyById(const v8::FunctionCallbackInfo<v8::Value> &info);

      private:
        static void JS_SetColor(const v8::FunctionCallbackInfo<v8::Value> &info);

        inline static Game::VehicleManager *gVehicles = nullptr;
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Vehicle>>> gClasses;
    };
} // namespace VantaMP::Scripting
