#include "bindings.h"

#include "game/player_manager.h"
#include "shared/player_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <scripting/builtins/player.h>
#include <scripting/engine.h>
#include <scripting/module.h>
#include <scripting/resource/resource_manager.h>
#include <v8pp/class.hpp>

namespace VantaMP::Scripting {
    namespace {
        Game::PlayerManager *gPlayers = nullptr;

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

            gPlayers->Spawn(player->ownerGUID, v8pp::from_v8<float>(isolate, info[0]), v8pp::from_v8<float>(isolate, info[1]), v8pp::from_v8<float>(isolate, info[2]));
        }

        std::string Nickname(Framework::Scripting::Builtins::Player &self) {
            auto *player = EntityOf(self);

            return player ? player->nickname : "";
        }
    } // namespace

    void Register(Framework::Scripting::Engine *engine, Game::PlayerManager &players) {
        if (!engine) {
            return;
        }

        gPlayers = &players;

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
