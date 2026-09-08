#include "scripting/player.h"

#include "game/sync/player_sync.h"
#include "scripting/vehicle.h"
#include "shared/entity/player_entity.h"

#include <core_modules.h>
#include <scripting/scripting_catalog.h>
#include <v8pp/convert.hpp>

namespace VantaMP::Scripting {
    Player::Player(std::uint64_t networkId): Framework::Scripting::Builtins::Player(networkId) {}

    Shared::PlayerEntity *Player::ResolvePlayer() const {
        return dynamic_cast<Shared::PlayerEntity *>(Resolve());
    }

    std::string Player::GetNickname() const {
        auto *player = ResolvePlayer();
        return player ? player->nickname : "";
    }

    float Player::GetHealth() const {
        auto *player = ResolvePlayer();
        return player ? player->health : 0.0f;
    }

    float Player::GetArmour() const {
        auto *player = ResolvePlayer();
        return player ? player->armour : 0.0f;
    }

    std::uint32_t Player::GetModel() const {
        auto *player = ResolvePlayer();
        return player ? player->modelId : 0;
    }

    std::int32_t Player::GetMoney() const {
        auto *player = ResolvePlayer();
        return player ? player->money : 0;
    }

    std::uint8_t Player::GetWantedLevel() const {
        auto *player = ResolvePlayer();
        return player ? player->wantedLevel : 0;
    }

    bool Player::GetFrozen() const {
        auto *player = ResolvePlayer();
        return player && player->frozen;
    }

    bool Player::GetCollisions() const {
        auto *player = ResolvePlayer();
        return !player || player->collisions;
    }

    std::uint8_t Player::GetFightingStyle() const {
        auto *player = ResolvePlayer();
        return player ? player->fightingStyle : 0;
    }

    float Player::GetOxygen() const {
        auto *player = ResolvePlayer();
        return player ? player->oxygen : 0.0f;
    }

    std::uint16_t Player::GetWalkingStyle() const {
        auto *player = ResolvePlayer();
        return player ? player->walkingStyle : 0;
    }

    std::uint8_t Player::GetAlpha() const {
        auto *player = ResolvePlayer();
        return player ? player->alpha : 255;
    }

    bool Player::GetOnFire() const {
        auto *player = ResolvePlayer();
        return player && player->onFire;
    }

    float Player::GetMaxHealth() const {
        auto *player = ResolvePlayer();
        return player ? player->maxHealth : 100.0f;
    }

    bool Player::GetJetPack() const {
        auto *player = ResolvePlayer();
        return player && player->hasJetPack;
    }

    bool Player::IsDrowning() const {
        auto *player = ResolvePlayer();
        return player && player->isDrowning;
    }

    std::uint32_t Player::GetWeapon() const {
        auto *player = ResolvePlayer();
        return player ? player->weaponType : 0;
    }

    std::uint32_t Player::GetAmmo() const {
        auto *player = ResolvePlayer();
        return player ? player->ammoTotal : 0;
    }

    std::uint32_t Player::GetAmmoInClip() const {
        auto *player = ResolvePlayer();
        return player ? player->ammoClip : 0;
    }

    bool Player::IsDead() const {
        auto *player = ResolvePlayer();
        return player && player->health <= 0.0f;
    }

    bool Player::IsDucked() const {
        auto *player = ResolvePlayer();
        return player && player->isDucked;
    }

    bool Player::IsInWater() const {
        auto *player = ResolvePlayer();
        return player && player->isInWater;
    }

    bool Player::IsInAir() const {
        auto *player = ResolvePlayer();
        return player && player->isInAir;
    }

    bool Player::IsInVehicle() const {
        auto *player = ResolvePlayer();
        return player && player->vehicleNetworkId != 0;
    }

    std::uint8_t Player::GetVehicleSeat() const {
        auto *player = ResolvePlayer();
        return player ? player->vehicleSeat : 0;
    }

    std::uint8_t Player::GetInterior() const {
        auto *player = ResolvePlayer();
        return player ? player->interior : 0;
    }

    std::uint16_t Player::GetDimension() const {
        auto *player = ResolvePlayer();
        return player ? player->dimension : 0;
    }

    v8::Local<v8::Object> Player::Wrap(v8::Isolate *isolate, std::uint64_t networkId) {
        GetClass(isolate);
        return v8pp::class_<Player>::create_object(isolate, networkId);
    }

    v8::Local<v8::Value> Player::CurrentLocal(v8::Isolate *isolate) {
        const auto id = Game::LocalNetworkId();
        if (id == 0) {
            return v8::Null(isolate);
        }
        return Wrap(isolate, id);
    }

    void Player::JS_GetLocal(const v8::FunctionCallbackInfo<v8::Value> &info) {
        info.GetReturnValue().Set(CurrentLocal(info.GetIsolate()));
    }

    void Player::JS_LocalPlayerGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        info.GetReturnValue().Set(CurrentLocal(info.GetIsolate()));
    }

    void Player::JS_GetOccupiedVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        auto *self    = v8pp::class_<Player>::unwrap_object(isolate, info.This());
        auto *player  = self ? self->ResolvePlayer() : nullptr;

        if (!player || player->vehicleNetworkId == 0) {
            info.GetReturnValue().SetNull();
            return;
        }

        info.GetReturnValue().Set(Vehicle::Wrap(isolate, player->vehicleNetworkId));
    }

    v8pp::class_<Player> &Player::GetClass(v8::Isolate *isolate) {
        auto found = gClasses.find(isolate);

        if (found != gClasses.end()) {
            return *found->second;
        }

        Framework::Scripting::Builtins::Player::GetClass(isolate);

        auto &cls = gClasses[isolate];
        cls       = std::make_unique<v8pp::class_<Player>>(isolate, Framework::Scripting::GetScriptingCatalog(isolate), "VantaPlayer",
                                                     "A streamed player on this client. Readonly replica of the "
                                                     "server-authoritative PlayerEntity, plus Framework nametag getters.");

        cls->inherit<Framework::Scripting::Builtins::Player>()
            .document_base("Player")
            .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity id.")},
                                                      "Wraps an existing streamed player; does not create one."))
            .property("nickname", &Player::GetNickname, v8pp::metadata::property_docs("string", "Display name."))
            .property("health", &Player::GetHealth, v8pp::metadata::property_docs("number", "Current health."))
            .property("armour", &Player::GetArmour, v8pp::metadata::property_docs("number", "Current armour."))
            .property("model", &Player::GetModel, v8pp::metadata::property_docs("number", "Ped model id."))
            .property("money", &Player::GetMoney, v8pp::metadata::property_docs("number", "Cash."))
            .property("wantedLevel", &Player::GetWantedLevel, v8pp::metadata::property_docs("number", "Wanted stars 0..6."))
            .property("frozen", &Player::GetFrozen, v8pp::metadata::property_docs("boolean", "Whether the ped is frozen."))
            .property("collisions", &Player::GetCollisions, v8pp::metadata::property_docs("boolean", "Whether collisions are enabled."))
            .property("fightingStyle", &Player::GetFightingStyle, v8pp::metadata::property_docs("number", "Fighting style id."))
            .property("oxygen", &Player::GetOxygen, v8pp::metadata::property_docs("number", "Underwater oxygen."))
            .property("walkingStyle", &Player::GetWalkingStyle, v8pp::metadata::property_docs("number", "Walking style id."))
            .property("alpha", &Player::GetAlpha, v8pp::metadata::property_docs("number", "Ped alpha 0..255."))
            .property("onFire", &Player::GetOnFire, v8pp::metadata::property_docs("boolean", "Whether the ped is on fire."))
            .property("maxHealth", &Player::GetMaxHealth, v8pp::metadata::property_docs("number", "Maximum health."))
            .property("jetpack", &Player::GetJetPack, v8pp::metadata::property_docs("boolean", "Whether the ped has a jetpack."))
            .property("drowning", &Player::IsDrowning, v8pp::metadata::property_docs("boolean", "Whether the ped is drowning."))
            .property("weapon", &Player::GetWeapon, v8pp::metadata::property_docs("number", "Current weapon type."))
            .property("ammo", &Player::GetAmmo, v8pp::metadata::property_docs("number", "Total ammo for the current weapon."))
            .property("ammoInClip", &Player::GetAmmoInClip, v8pp::metadata::property_docs("number", "Ammo in the current clip."))
            .property("dead", &Player::IsDead, v8pp::metadata::property_docs("boolean", "Whether health is at or below zero."))
            .property("ducked", &Player::IsDucked, v8pp::metadata::property_docs("boolean", "Whether the ped is ducking."))
            .property("inWater", &Player::IsInWater, v8pp::metadata::property_docs("boolean", "Whether the ped is in water."))
            .property("inAir", &Player::IsInAir, v8pp::metadata::property_docs("boolean", "Whether the ped is in the air."))
            .property("inVehicle", &Player::IsInVehicle, v8pp::metadata::property_docs("boolean", "Whether the ped is in a vehicle."))
            .property("vehicleSeat", &Player::GetVehicleSeat, v8pp::metadata::property_docs("number", "Seat index when in a vehicle."))
            .property("interior", &Player::GetInterior, v8pp::metadata::property_docs("number", "Interior id."))
            .property("dimension", &Player::GetDimension, v8pp::metadata::property_docs("number", "Dimension id."));

        cls->prototype_function("getOccupiedVehicle", &Player::JS_GetOccupiedVehicle,
                                v8pp::metadata::docs("Vehicle | null", {}, "Returns the vehicle this player occupies, if any.", "Vehicle handle or null."));

        return *cls;
    }

    void Player::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &cls  = GetClass(isolate);
        auto  ctx  = isolate->GetCurrentContext();
        auto  ctor = cls.js_function_template()->GetFunction(ctx).ToLocalChecked();

        cls.static_function(ctor, "getLocal", &Player::JS_GetLocal,
                            v8pp::metadata::docs("Player | null", {}, "Returns the local player's handle once streamed.", "Local Player, or null before spawn."));

        global->Set(ctx, v8pp::to_v8(isolate, "Player"), ctor).Check();

        // Live binding: always resolves against Game::LocalNetworkId().
        global
            ->SetNativeDataProperty(ctx, v8pp::to_v8(isolate, "localPlayer").As<v8::Name>(), &Player::JS_LocalPlayerGetter, nullptr,
                                    v8::Local<v8::Value>(), static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete))
            .Check();
    }
} // namespace VantaMP::Scripting
