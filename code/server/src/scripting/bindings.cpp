#include "bindings.h"

#include "game/player_manager.h"
#include "game/vehicle_manager.h"
#include "shared/player_entity.h"
#include "shared/vehicle_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <scripting/builtins/player.h>
#include <scripting/engine.h>
#include <scripting/module.h>
#include <scripting/resource/resource_manager.h>
#include <v8pp/class.hpp>

namespace VantaMP::Scripting {
    namespace {
        Game::PlayerManager  *gPlayers  = nullptr;
        Game::VehicleManager *gVehicles = nullptr;

        Shared::PlayerEntity *EntityOf(Framework::Scripting::Builtins::Player &self) {
            return dynamic_cast<Shared::PlayerEntity *>(self.GetHandle());
        }

        // v8pp's class_::function treats a free function as taking EVERY
        // argument from JS. Player& as the first parameter therefore wanted
        // player.spawn(player, x, y, z). Taking the callback info instead
        // lets us read this and the three numbers the script actually passes.
        void Spawn(v8::FunctionCallbackInfo<v8::Value> const &info) {
            auto *isolate = info.GetIsolate();
            auto *self    = v8pp::class_<Framework::Scripting::Builtins::Player>::unwrap_object(isolate, info.This());
            auto *player  = self ? EntityOf(*self) : nullptr;

            if (!player || !gPlayers || info.Length() < 3) {
                return;
            }

            const auto heading   = info.Length() > 3 ? v8pp::from_v8<float>(isolate, info[3]) : 0.0f;
            const auto model     = info.Length() > 4 ? v8pp::from_v8<std::uint32_t>(isolate, info[4]) : 0u;
            const auto health    = info.Length() > 5 ? v8pp::from_v8<float>(isolate, info[5]) : 100.0f;
            const auto armour    = info.Length() > 6 ? v8pp::from_v8<float>(isolate, info[6]) : 0.0f;
            const auto interior  = info.Length() > 7 ? v8pp::from_v8<std::uint8_t>(isolate, info[7]) : std::uint8_t {0};
            const auto dimension = info.Length() > 8 ? v8pp::from_v8<std::uint16_t>(isolate, info[8]) : std::uint16_t {0};

            gPlayers->Spawn(player->ownerGUID, v8pp::from_v8<float>(isolate, info[0]), v8pp::from_v8<float>(isolate, info[1]), v8pp::from_v8<float>(isolate, info[2]),
                            heading, model, health, armour, interior, dimension);
        }

        std::string Nickname(Framework::Scripting::Builtins::Player &self) {
            auto *player = EntityOf(self);

            return player ? player->nickname : "";
        }

        std::uint8_t Interior(Framework::Scripting::Builtins::Player &self) {
            auto *player = EntityOf(self);

            return player ? player->interior : 0;
        }

        void SetInterior(Framework::Scripting::Builtins::Player &self, std::uint8_t interior) {
            if (auto *player = EntityOf(self); player && gPlayers) {
                gPlayers->SetInterior(*player, interior);
            }
        }

        std::uint16_t Dimension(Framework::Scripting::Builtins::Player &self) {
            auto *player = EntityOf(self);

            return player ? player->dimension : 0;
        }

        void SetDimension(Framework::Scripting::Builtins::Player &self, std::uint16_t dimension) {
            if (auto *player = EntityOf(self); player && gPlayers) {
                gPlayers->SetDimension(*player, dimension);
            }
        }

        void CreateVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();

            if (!gVehicles || info.Length() < 4) {
                info.GetReturnValue().Set(v8::Integer::New(isolate, 0));
                return;
            }

            const auto model   = v8pp::from_v8<std::uint32_t>(isolate, info[0]);
            const auto x       = v8pp::from_v8<float>(isolate, info[1]);
            const auto y       = v8pp::from_v8<float>(isolate, info[2]);
            const auto z       = v8pp::from_v8<float>(isolate, info[3]);
            const auto heading    = info.Length() > 4 ? v8pp::from_v8<float>(isolate, info[4]) : 0.0f;
            const auto primary    = info.Length() > 5 ? v8pp::from_v8<std::uint8_t>(isolate, info[5]) : std::uint8_t {0};
            const auto secondary  = info.Length() > 6 ? v8pp::from_v8<std::uint8_t>(isolate, info[6]) : std::uint8_t {0};
            const auto interior   = info.Length() > 7 ? v8pp::from_v8<std::uint8_t>(isolate, info[7]) : std::uint8_t {0};
            const auto dimension  = info.Length() > 8 ? v8pp::from_v8<std::uint16_t>(isolate, info[8]) : std::uint16_t {0};

            const auto id = gVehicles->Create(model, x, y, z, heading, primary, secondary, interior, dimension);

            info.GetReturnValue().Set(v8::Number::New(isolate, static_cast<double>(id)));
        }

        void DestroyVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (!gVehicles || info.Length() < 1) {
                return;
            }

            gVehicles->Destroy(v8pp::from_v8<std::uint64_t>(info.GetIsolate(), info[0]));
        }

        void SetVehicleInterior(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (!gVehicles || info.Length() < 2) {
                return;
            }

            auto *isolate = info.GetIsolate();
            auto *vehicle = gVehicles->Find(v8pp::from_v8<std::uint64_t>(isolate, info[0]));

            if (vehicle) {
                gVehicles->SetInterior(*vehicle, v8pp::from_v8<std::uint8_t>(isolate, info[1]));
            }
        }

        void SetVehicleDimension(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (!gVehicles || info.Length() < 2) {
                return;
            }

            auto *isolate = info.GetIsolate();
            auto *vehicle = gVehicles->Find(v8pp::from_v8<std::uint64_t>(isolate, info[0]));

            if (vehicle) {
                gVehicles->SetDimension(*vehicle, v8pp::from_v8<std::uint16_t>(isolate, info[1]));
            }
        }

        void GetVehicleInterior(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *vehicle = gVehicles && info.Length() >= 1 ? gVehicles->Find(v8pp::from_v8<std::uint64_t>(isolate, info[0])) : nullptr;

            info.GetReturnValue().Set(v8::Integer::New(isolate, vehicle ? vehicle->interior : 0));
        }

        void GetVehicleDimension(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto *vehicle = gVehicles && info.Length() >= 1 ? gVehicles->Find(v8pp::from_v8<std::uint64_t>(isolate, info[0])) : nullptr;

            info.GetReturnValue().Set(v8::Integer::New(isolate, vehicle ? vehicle->dimension : 0));
        }
    } // namespace

    void Register(Framework::Scripting::Engine *engine, Game::PlayerManager &players, Game::VehicleManager &vehicles) {
        if (!engine) {
            return;
        }

        gPlayers  = &players;
        gVehicles = &vehicles;

        // InitFrameworkSDK calls this with no V8 scopes of its own. Creating
        // or extending a v8pp class without a locker and a handle scope is
        // an access violation inside libnode - isolate TLS is empty, and the
        // first heap read lands at a low offset (0x1f0 on this crash).
        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Context::Scope contextScope(engine->GetContext());

        // The same Player class the framework already publishes. Adding here
        // rather than wrapping our own type means Events.on('playerReady')
        // receives an object that already has kick, emit and id.
        auto &cls = Framework::Scripting::Builtins::Player::GetClass(isolate);

        cls.function("spawn", &Spawn);
        cls.property("nickname", &Nickname);
        cls.property("interior", &Interior, &SetInterior);
        cls.property("dimension", &Dimension, &SetDimension);

        v8::Local<v8::Context> context = engine->GetContext();
        v8::Local<v8::Object> vehiclesObj = v8::Object::New(isolate);

        vehiclesObj->Set(context, v8pp::to_v8(isolate, "create"), v8::FunctionTemplate::New(isolate, &CreateVehicle)->GetFunction(context).ToLocalChecked()).Check();
        vehiclesObj->Set(context, v8pp::to_v8(isolate, "destroy"), v8::FunctionTemplate::New(isolate, &DestroyVehicle)->GetFunction(context).ToLocalChecked()).Check();
        vehiclesObj->Set(context, v8pp::to_v8(isolate, "setInterior"), v8::FunctionTemplate::New(isolate, &SetVehicleInterior)->GetFunction(context).ToLocalChecked()).Check();
        vehiclesObj->Set(context, v8pp::to_v8(isolate, "setDimension"), v8::FunctionTemplate::New(isolate, &SetVehicleDimension)->GetFunction(context).ToLocalChecked()).Check();
        vehiclesObj->Set(context, v8pp::to_v8(isolate, "getInterior"), v8::FunctionTemplate::New(isolate, &GetVehicleInterior)->GetFunction(context).ToLocalChecked()).Check();
        vehiclesObj->Set(context, v8pp::to_v8(isolate, "getDimension"), v8::FunctionTemplate::New(isolate, &GetVehicleDimension)->GetFunction(context).ToLocalChecked()).Check();
        context->Global()->Set(context, v8pp::to_v8(isolate, "Vehicles"), vehiclesObj).Check();
    }

    bool EmitPlayerReady(std::uint64_t networkId) {
        auto *module = Framework::CoreModules::GetScriptingModule();

        if (!module) {
            return false;
        }

        auto *engine = module->GetScriptingEngine();
        auto *resources = module->GetResourceManager();

        if (!engine || !resources || !engine->IsInitialized()) {
            return false;
        }

        if (resources->GetEvents().GetListenerCount("playerReady") == 0) {
            return false;
        }

        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        v8::Context::Scope contextScope(context);

        Framework::Scripting::Builtins::Player::GetClass(isolate);

        std::vector<v8::Local<v8::Value>> args;
        args.push_back(v8pp::class_<Framework::Scripting::Builtins::Player>::create_object(isolate, networkId));

        resources->GetEvents().EmitReserved(isolate, context, "playerReady", args);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("playerReady handed to resources for {}", networkId);

        return true;
    }

    bool EmitPlayerDeath(std::uint64_t networkId, std::uint64_t killerId, std::uint32_t weaponType) {
        auto *module = Framework::CoreModules::GetScriptingModule();

        if (!module) {
            return false;
        }

        auto *engine    = module->GetScriptingEngine();
        auto *resources = module->GetResourceManager();

        if (!engine || !resources || !engine->IsInitialized()) {
            return false;
        }

        if (resources->GetEvents().GetListenerCount("playerDeath") == 0) {
            return false;
        }

        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        v8::Context::Scope contextScope(context);

        Framework::Scripting::Builtins::Player::GetClass(isolate);

        std::vector<v8::Local<v8::Value>> args;
        args.push_back(v8pp::class_<Framework::Scripting::Builtins::Player>::create_object(isolate, networkId));
        args.push_back(v8pp::to_v8(isolate, killerId));
        args.push_back(v8pp::to_v8(isolate, weaponType));

        resources->GetEvents().EmitReserved(isolate, context, "playerDeath", args);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("playerDeath handed to resources for {} (killer {} weapon {})", networkId, killerId, weaponType);

        return true;
    }
} // namespace VantaMP::Scripting
