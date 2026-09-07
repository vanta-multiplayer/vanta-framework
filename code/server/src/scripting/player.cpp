#include "player.h"

#include "game/player_manager.h"
#include "shared/entity/player_entity.h"

#include <scripting/builtins/vector3.h>
#include <glm/gtc/quaternion.hpp>

#include <core_modules.h>
#include <logging/logger.h>
#include <scripting/engine.h>
#include <scripting/module.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/scripting_catalog.h>
#include <v8pp/convert.hpp>

namespace VantaMP::Scripting {
    Player::Player(std::uint64_t networkId): Framework::Scripting::Builtins::Player(networkId) {}

    Shared::PlayerEntity *Player::ResolvePlayer() const {
        return dynamic_cast<Shared::PlayerEntity *>(GetHandle());
    }

    std::string Player::GetNickname() const {
        auto *player = ResolvePlayer();

        return player ? player->nickname : "";
    }

    std::uint8_t Player::GetInterior() const {
        auto *player = ResolvePlayer();

        return player ? player->interior : 0;
    }

    void Player::SetInterior(std::uint8_t interior) {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            gPlayers->SetInterior(*player, interior);
        }
    }

    std::uint16_t Player::GetDimension() const {
        auto *player = ResolvePlayer();

        return player ? player->dimension : 0;
    }

    void Player::SetDimension(std::uint16_t dimension) {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            gPlayers->SetDimension(*player, dimension);
        }
    }

    void Player::Bind(Game::PlayerManager &players) {
        gPlayers = &players;
    }

    v8::Local<v8::Object> Player::Wrap(v8::Isolate *isolate, std::uint64_t networkId) {
        GetClass(isolate);
        return v8pp::class_<Player>::create_object(isolate, networkId);
    }

    void Player::JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        auto *self    = v8pp::class_<Player>::unwrap_object(isolate, info.This());
        auto *player  = self ? self->ResolvePlayer() : nullptr;

        if (!player || !gPlayers) {
            return;
        }

        float x       = player->position.x;
        float y       = player->position.y;
        float z       = player->position.z;
        float heading = glm::roll(player->rotation);
        int   arg     = 0;

        if (info.Length() >= 1) {
            if (auto *pos = v8pp::class_<Framework::Scripting::Builtins::Vector3>::unwrap_object(isolate, info[0])) {
                x   = pos->getX();
                y   = pos->getY();
                z   = pos->getZ();
                arg = 1;
            }
            else if (info.Length() >= 3) {
                x   = v8pp::from_v8<float>(isolate, info[0]);
                y   = v8pp::from_v8<float>(isolate, info[1]);
                z   = v8pp::from_v8<float>(isolate, info[2]);
                arg = 3;
            }
        }

        if (info.Length() > arg) {
            heading = v8pp::from_v8<float>(isolate, info[arg]);
        }

        const auto model     = info.Length() > arg + 1 ? v8pp::from_v8<std::uint32_t>(isolate, info[arg + 1]) : 0u;
        const auto health    = info.Length() > arg + 2 ? v8pp::from_v8<float>(isolate, info[arg + 2]) : 100.0f;
        const auto armour    = info.Length() > arg + 3 ? v8pp::from_v8<float>(isolate, info[arg + 3]) : 0.0f;
        const auto interior  = info.Length() > arg + 4 ? v8pp::from_v8<std::uint8_t>(isolate, info[arg + 4]) : std::uint8_t {0};
        const auto dimension = info.Length() > arg + 5 ? v8pp::from_v8<std::uint16_t>(isolate, info[arg + 5]) : std::uint16_t {0};

        gPlayers->Spawn(player->ownerGUID, x, y, z, heading, model, health, armour, interior, dimension);
    }

    v8pp::class_<Player> &Player::GetClass(v8::Isolate *isolate) {
        auto found = gClasses.find(isolate);

        if (found != gClasses.end()) {
            return *found->second;
        }

        Framework::Scripting::Builtins::Player::GetClass(isolate);

        auto &cls = gClasses[isolate];
        cls       = std::make_unique<v8pp::class_<Player>>(isolate, Framework::Scripting::GetScriptingCatalog(isolate), "VantaPlayer",
                                                     "A connected player. Framework Player (kick, emit, ping, nametag) "
                                                     "plus spawn, nickname, interior and dimension. Position and "
                                                     "rotation are the Entity ones.");

        cls->inherit<Framework::Scripting::Builtins::Player>()
            .ctor<std::uint64_t>()
            .property("nickname", &Player::GetNickname)
            .property("interior", &Player::GetInterior, &Player::SetInterior)
            .property("dimension", &Player::GetDimension, &Player::SetDimension);

        cls->prototype_function("spawn", &Player::JS_Spawn);

        return *cls;
    }

    void Player::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &cls  = GetClass(isolate);
        auto  ctx  = isolate->GetCurrentContext();
        auto  ctor = cls.js_function_template()->GetFunction(ctx).ToLocalChecked();

        global->Set(ctx, v8pp::to_v8(isolate, "Player"), ctor).Check();
    }

    bool Player::EmitReady(std::uint64_t networkId) {
        auto *module = Framework::CoreModules::GetScriptingModule();

        if (!module) {
            return false;
        }

        auto *engine    = module->GetScriptingEngine();
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

        std::vector<v8::Local<v8::Value>> args;
        args.push_back(Wrap(isolate, networkId));

        resources->GetEvents().EmitReserved(isolate, context, "playerReady", args);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("playerReady handed to resources for {}", networkId);

        return true;
    }

    bool Player::EmitDeath(std::uint64_t networkId, std::uint64_t killerId, std::uint32_t weaponType) {
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

        std::vector<v8::Local<v8::Value>> args;
        args.push_back(Wrap(isolate, networkId));
        args.push_back(v8pp::to_v8(isolate, killerId));
        args.push_back(v8pp::to_v8(isolate, weaponType));

        resources->GetEvents().EmitReserved(isolate, context, "playerDeath", args);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("playerDeath handed to resources for {} (killer {} weapon {})", networkId, killerId, weaponType);

        return true;
    }
} // namespace VantaMP::Scripting
