#include "player.h"

#include "game/player_manager.h"
#include "scripting/vehicle.h"
#include "shared/catalog/model_catalog.h"
#include "shared/entity/player_entity.h"
#include "shared/ped/weapon_slot.h"
#include "shared/rpc/player/script.h"
#include "shared/rpc/player/weapon.h"

#include <scripting/builtins/vector3.h>
#include <glm/gtc/quaternion.hpp>

#include <core_modules.h>
#include <logging/logger.h>
#include <scripting/engine.h>
#include <scripting/module.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/scripting_catalog.h>
#include <v8pp/convert.hpp>

#include <algorithm>

namespace VantaMP::Scripting {
    Player::Player(std::uint64_t networkId): Framework::Scripting::Builtins::Player(networkId) {}

    Shared::PlayerEntity *Player::ResolvePlayer() const {
        return dynamic_cast<Shared::PlayerEntity *>(GetHandle());
    }

    std::string Player::GetNickname() const {
        auto *player = ResolvePlayer();

        return player ? player->nickname : "";
    }

    void Player::SetNickname(const std::string &nickname) {
        if (auto *player = ResolvePlayer()) {
            player->nickname = nickname;
        }
    }

    float Player::GetHealth() const {
        auto *player = ResolvePlayer();

        return player ? player->health : 0.0f;
    }

    void Player::SetHealth(float health) {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            player->health = health;
            gPlayers->SendScript(*player, Shared::kPlayerScriptHealth);
        }
    }

    float Player::GetArmour() const {
        auto *player = ResolvePlayer();

        return player ? player->armour : 0.0f;
    }

    void Player::SetArmour(float armour) {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            player->armour = armour;
            gPlayers->SendScript(*player, Shared::kPlayerScriptArmour);
        }
    }

    std::uint32_t Player::GetModel() const {
        auto *player = ResolvePlayer();

        return player ? player->modelId : 0;
    }

    void Player::SetModel(std::uint32_t modelId) {
        if (auto *player = ResolvePlayer(); player && gPlayers && Shared::ModelCatalog::IsPed(modelId)) {
            player->modelId = modelId;
            gPlayers->SendScript(*player, Shared::kPlayerScriptModel);
        }
    }

    std::int32_t Player::GetMoney() const {
        auto *player = ResolvePlayer();

        return player ? player->money : 0;
    }

    void Player::SetMoney(std::int32_t money) {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            player->money = money;
            gPlayers->SendScript(*player, Shared::kPlayerScriptMoney);
        }
    }

    std::uint8_t Player::GetWantedLevel() const {
        auto *player = ResolvePlayer();

        return player ? player->wantedLevel : 0;
    }

    void Player::SetWantedLevel(std::uint8_t stars) {
        if (auto *player = ResolvePlayer(); player && gPlayers && stars <= 6) {
            player->wantedLevel = stars;
            gPlayers->SendScript(*player, Shared::kPlayerScriptWanted);
        }
    }

    bool Player::GetFrozen() const {
        auto *player = ResolvePlayer();

        return player && player->frozen;
    }

    void Player::SetFrozen(bool frozen) {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            player->frozen = frozen;
            gPlayers->SendScript(*player, Shared::kPlayerScriptFrozen);
        }
    }

    bool Player::GetCollisions() const {
        auto *player = ResolvePlayer();

        return !player || player->collisions;
    }

    void Player::SetCollisions(bool enabled) {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            player->collisions = enabled;
            gPlayers->SendScript(*player, 0, Shared::kPlayerScript2Collisions);
        }
    }

    std::uint8_t Player::GetFightingStyle() const {
        auto *player = ResolvePlayer();

        return player ? player->fightingStyle : 0;
    }

    void Player::SetFightingStyle(std::uint8_t style) {
        // GTA eFightingStyle: 4 standard through 16 elbows.
        if (auto *player = ResolvePlayer(); player && gPlayers && style >= 4 && style <= 16) {
            player->fightingStyle = style;
            gPlayers->SendScript(*player, 0, Shared::kPlayerScript2FightingStyle);
        }
    }

    float Player::GetOxygen() const {
        auto *player = ResolvePlayer();

        return player ? player->oxygen : 0.0f;
    }

    void Player::SetOxygen(float oxygen) {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            player->oxygen = std::clamp(oxygen, 0.0f, 1000.0f);
            gPlayers->SendScript(*player, 0, Shared::kPlayerScript2Oxygen);
        }
    }

    std::uint16_t Player::GetWalkingStyle() const {
        auto *player = ResolvePlayer();

        return player ? player->walkingStyle : 0;
    }

    void Player::SetWalkingStyle(std::uint16_t style) {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            player->walkingStyle = style;
            gPlayers->SendScript(*player, 0, Shared::kPlayerScript2WalkingStyle);
        }
    }

    std::uint8_t Player::GetAlpha() const {
        auto *player = ResolvePlayer();

        return player ? player->alpha : 255;
    }

    void Player::SetAlpha(std::uint8_t alpha) {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            player->alpha = alpha;
            gPlayers->SendScript(*player, 0, Shared::kPlayerScript2Alpha);
        }
    }

    bool Player::GetOnFire() const {
        auto *player = ResolvePlayer();

        return player && player->onFire;
    }

    void Player::SetOnFire(bool on) {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            player->onFire = on;
            gPlayers->SendScript(*player, 0, Shared::kPlayerScript2OnFire);
        }
    }

    float Player::GetMaxHealth() const {
        auto *player = ResolvePlayer();

        return player ? player->maxHealth : 100.0f;
    }

    void Player::SetMaxHealth(float maxHealth) {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            player->maxHealth = std::max(1.0f, maxHealth);
            gPlayers->SendScript(*player, 0, Shared::kPlayerScript2MaxHealth);
        }
    }

    bool Player::GetJetPack() const {
        auto *player = ResolvePlayer();

        return player && player->hasJetPack;
    }

    void Player::SetJetPack(bool hasJetPack) {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            player->hasJetPack = hasJetPack;
            gPlayers->SendScript(*player, 0, Shared::kPlayerScript2HasJetPack);
        }
    }

    bool Player::IsOnGround() const {
        auto *player = ResolvePlayer();

        return player && !player->isInAir && !player->isInWater;
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

    std::uint8_t Player::GetWeaponSlot() const {
        auto *player = ResolvePlayer();

        return player ? Shared::WeaponSlot(player->weaponType) : 0;
    }

    void Player::SetWeaponSlot(std::uint8_t slot) {
        if (auto *player = ResolvePlayer(); player && gPlayers && slot <= 12) {
            gPlayers->SendWeapon(*player, Shared::RPC::PlayerWeapon::kSetSlot, slot, 0);
        }
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

    void Player::GiveWeapon(std::uint32_t weaponType, std::uint32_t ammo) {
        if (auto *player = ResolvePlayer(); player && gPlayers && Shared::ModelCatalog::IsWeapon(weaponType)) {
            gPlayers->SendWeapon(*player, Shared::RPC::PlayerWeapon::kGive, weaponType, ammo);
        }
    }

    void Player::TakeWeapon(std::uint32_t weaponType) {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            gPlayers->SendWeapon(*player, Shared::RPC::PlayerWeapon::kTake, weaponType, 0);
        }
    }

    void Player::TakeAllWeapons() {
        if (auto *player = ResolvePlayer(); player && gPlayers) {
            gPlayers->SendWeapon(*player, Shared::RPC::PlayerWeapon::kTakeAll, 0, 0);
        }
    }

    void Player::SetWeaponAmmo(std::uint32_t weaponType, std::uint32_t ammo) {
        if (auto *player = ResolvePlayer(); player && gPlayers && Shared::ModelCatalog::IsWeapon(weaponType)) {
            gPlayers->SendWeapon(*player, Shared::RPC::PlayerWeapon::kSetAmmo, weaponType, ammo);
        }
    }

    void Player::Kill() {
        SetHealth(0.0f);
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

        const auto hasNumber = [&](int index) {
            return info.Length() > index && info[index]->IsNumber();
        };

        if (hasNumber(arg)) {
            heading = static_cast<float>(info[arg]->NumberValue(isolate->GetCurrentContext()).FromMaybe(heading));
        }

        // Optional trailing args: skip undefined/null so JS `spawn(x,y,z,h, undefined, hp)`
        // does not throw from from_v8 and abort the process (0xE06D7363).
        const auto model     = hasNumber(arg + 1) ? static_cast<std::uint32_t>(info[arg + 1]->NumberValue(isolate->GetCurrentContext()).FromMaybe(0)) : 0u;
        const auto health    = hasNumber(arg + 2) ? static_cast<float>(info[arg + 2]->NumberValue(isolate->GetCurrentContext()).FromMaybe(100.0)) : 100.0f;
        const auto armour    = hasNumber(arg + 3) ? static_cast<float>(info[arg + 3]->NumberValue(isolate->GetCurrentContext()).FromMaybe(0.0)) : 0.0f;
        const auto interior  = hasNumber(arg + 4) ? static_cast<std::uint8_t>(info[arg + 4]->NumberValue(isolate->GetCurrentContext()).FromMaybe(0)) : std::uint8_t {0};
        const auto dimension = hasNumber(arg + 5) ? static_cast<std::uint16_t>(info[arg + 5]->NumberValue(isolate->GetCurrentContext()).FromMaybe(0)) : std::uint16_t {0};

        gPlayers->Spawn(player->ownerGUID, x, y, z, heading, model, health, armour, interior, dimension);
    }

    void Player::JS_GetOccupiedVehicle(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        auto *self    = v8pp::class_<Player>::unwrap_object(isolate, info.This());
        auto *player  = self ? self->ResolvePlayer() : nullptr;

        if (!player || player->vehicleNetworkId == 0) {
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
                                                     "A connected player. Framework Player (kick, emit, ping, nametag) "
                                                     "plus spawn, vitals, money, wanted, weapons and occupancy.");

        cls->inherit<Framework::Scripting::Builtins::Player>()
            .document_base("Player")
            .ctor<std::uint64_t>()
            .property("nickname", &Player::GetNickname, &Player::SetNickname)
            .property("health", &Player::GetHealth, &Player::SetHealth)
            .property("armour", &Player::GetArmour, &Player::SetArmour)
            .property("model", &Player::GetModel, &Player::SetModel)
            .property("money", &Player::GetMoney, &Player::SetMoney)
            .property("wantedLevel", &Player::GetWantedLevel, &Player::SetWantedLevel)
            .property("frozen", &Player::GetFrozen, &Player::SetFrozen)
            .property("collisions", &Player::GetCollisions, &Player::SetCollisions)
            .property("fightingStyle", &Player::GetFightingStyle, &Player::SetFightingStyle)
            .property("oxygen", &Player::GetOxygen, &Player::SetOxygen)
            .property("walkingStyle", &Player::GetWalkingStyle, &Player::SetWalkingStyle)
            .property("alpha", &Player::GetAlpha, &Player::SetAlpha)
            .property("onFire", &Player::GetOnFire, &Player::SetOnFire)
            .property("maxHealth", &Player::GetMaxHealth, &Player::SetMaxHealth)
            .property("jetpack", &Player::GetJetPack, &Player::SetJetPack)
            .property("onGround", &Player::IsOnGround)
            .property("drowning", &Player::IsDrowning)
            .property("weapon", &Player::GetWeapon)
            .property("ammo", &Player::GetAmmo)
            .property("ammoInClip", &Player::GetAmmoInClip)
            .property("weaponSlot", &Player::GetWeaponSlot, &Player::SetWeaponSlot)
            .property("dead", &Player::IsDead)
            .property("ducked", &Player::IsDucked)
            .property("inWater", &Player::IsInWater)
            .property("inAir", &Player::IsInAir)
            .property("inVehicle", &Player::IsInVehicle)
            .property("vehicleSeat", &Player::GetVehicleSeat)
            .property("interior", &Player::GetInterior, &Player::SetInterior)
            .property("dimension", &Player::GetDimension, &Player::SetDimension)
            .function("giveWeapon", &Player::GiveWeapon)
            .function("takeWeapon", &Player::TakeWeapon)
            .function("takeAllWeapons", &Player::TakeAllWeapons)
            .function("setWeaponAmmo", &Player::SetWeaponAmmo)
            .function("kill", &Player::Kill);

        cls->prototype_function("spawn", &Player::JS_Spawn,
                                v8pp::metadata::docs("void",
                                    {v8pp::metadata::param("xOrPos", "number | Vector3", false, "X or a Vector3 position."),
                                        v8pp::metadata::param("y", "number", true, "Y when position is components."),
                                        v8pp::metadata::param("z", "number", true, "Z when position is components."),
                                        v8pp::metadata::param("heading", "number", true, "Optional heading radians."),
                                        v8pp::metadata::param("model", "number", true, "Optional ped model."),
                                        v8pp::metadata::param("health", "number", true, "Optional health."),
                                        v8pp::metadata::param("armour", "number", true, "Optional armour."),
                                        v8pp::metadata::param("interior", "number", true, "Optional interior."),
                                        v8pp::metadata::param("dimension", "number", true, "Optional dimension.")},
                                    "Spawns the player at a position."));
        cls->prototype_function("getOccupiedVehicle", &Player::JS_GetOccupiedVehicle,
                                v8pp::metadata::docs("Vehicle | null", {}, "Returns the vehicle this player occupies, if any.", "Vehicle handle or null."));

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

        // Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("playerReady handed to resources for {}", networkId);

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

        // Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("playerDeath handed to resources for {} (killer {} weapon {})", networkId, killerId, weaponType);

        return true;
    }
} // namespace VantaMP::Scripting
