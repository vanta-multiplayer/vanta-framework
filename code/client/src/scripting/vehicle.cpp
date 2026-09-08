#include "scripting/vehicle.h"

#include "scripting/player.h"
#include "shared/entity/player_entity.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/vehicle/colors.h"
#include "shared/vehicle/rotation.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/scripting_catalog.h>
#include <v8pp/convert.hpp>

namespace VantaMP::Scripting {
    namespace {
        using Framework::Scripting::Builtins::Color;
        using Framework::Scripting::Builtins::Vector3;

        Color UnpackColor(std::uint32_t packed) {
            return Color::fromRGB(static_cast<float>(Shared::RgbR(packed)), static_cast<float>(Shared::RgbG(packed)),
                                  static_cast<float>(Shared::RgbB(packed)));
        }

        Color ColorOf(const Shared::VehicleEntity *vehicle, int slot) {
            if (!vehicle || slot < 0 || slot >= Shared::kVehicleColorCount) {
                return Color::black();
            }

            return UnpackColor(vehicle->colours[static_cast<std::size_t>(slot)]);
        }
    } // namespace

    Vehicle::Vehicle(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

    Shared::VehicleEntity *Vehicle::ResolveVehicle() const {
        return dynamic_cast<Shared::VehicleEntity *>(Resolve());
    }

    std::uint32_t Vehicle::GetModel() const {
        auto *vehicle = ResolveVehicle();
        return vehicle ? vehicle->modelId : 0;
    }

    Color Vehicle::GetColor1() const {
        return ColorOf(ResolveVehicle(), 0);
    }

    Color Vehicle::GetColor2() const {
        return ColorOf(ResolveVehicle(), 1);
    }

    Color Vehicle::GetColor3() const {
        return ColorOf(ResolveVehicle(), 2);
    }

    Color Vehicle::GetColor4() const {
        return ColorOf(ResolveVehicle(), 3);
    }

    std::uint8_t Vehicle::GetPaintjob() const {
        auto *vehicle = ResolveVehicle();
        return vehicle ? vehicle->paintjob : 0;
    }

    float Vehicle::GetHealth() const {
        auto *vehicle = ResolveVehicle();
        return vehicle ? vehicle->health : 0.0f;
    }

    bool Vehicle::GetEngine() const {
        auto *vehicle = ResolveVehicle();
        return vehicle && vehicle->engineOn;
    }

    bool Vehicle::GetSiren() const {
        auto *vehicle = ResolveVehicle();
        return vehicle && vehicle->sirenOn;
    }

    bool Vehicle::GetLocked() const {
        auto *vehicle = ResolveVehicle();
        return vehicle && vehicle->locked;
    }

    bool Vehicle::GetBlown() const {
        auto *vehicle = ResolveVehicle();
        return vehicle && vehicle->blown;
    }

    std::string Vehicle::GetPlateText() const {
        auto *vehicle = ResolveVehicle();
        return vehicle ? vehicle->plateText : "";
    }

    std::uint8_t Vehicle::GetDirtLevel() const {
        auto *vehicle = ResolveVehicle();
        return vehicle ? vehicle->dirtLevel : 0;
    }

    bool Vehicle::GetTaxiLight() const {
        auto *vehicle = ResolveVehicle();
        return vehicle && vehicle->taxiLight;
    }

    bool Vehicle::GetFrozen() const {
        auto *vehicle = ResolveVehicle();
        return vehicle && vehicle->frozen;
    }

    bool Vehicle::GetDamageProof() const {
        auto *vehicle = ResolveVehicle();
        return vehicle && vehicle->damageProof;
    }

    bool Vehicle::GetCollisions() const {
        auto *vehicle = ResolveVehicle();
        return !vehicle || vehicle->collisions;
    }

    bool Vehicle::GetLandingGearDown() const {
        auto *vehicle = ResolveVehicle();
        return !vehicle || vehicle->landingGearDown;
    }

    bool Vehicle::GetLights() const {
        auto *vehicle = ResolveVehicle();
        return vehicle && vehicle->lightsOn;
    }

    std::uint8_t Vehicle::GetAlpha() const {
        auto *vehicle = ResolveVehicle();
        return vehicle ? vehicle->alpha : 255;
    }

    bool Vehicle::IsOnGround() const {
        auto *vehicle = ResolveVehicle();
        return !vehicle || vehicle->onGround;
    }

    bool Vehicle::GetEngineBroken() const {
        auto *vehicle = ResolveVehicle();
        return vehicle && vehicle->engineBroken;
    }

    std::uint8_t Vehicle::GetInterior() const {
        auto *vehicle = ResolveVehicle();
        return vehicle ? vehicle->interior : 0;
    }

    std::uint16_t Vehicle::GetDimension() const {
        auto *vehicle = ResolveVehicle();
        return vehicle ? vehicle->dimension : 0;
    }

    Vector3 Vehicle::GetVelocity() const {
        auto *vehicle = ResolveVehicle();
        return vehicle ? Vector3(vehicle->velocity.x, vehicle->velocity.y, vehicle->velocity.z) : Vector3::zero();
    }

    Vector3 Vehicle::GetTurnVelocity() const {
        auto *vehicle = ResolveVehicle();
        return vehicle ? Vector3(vehicle->angularVelocity.x, vehicle->angularVelocity.y, vehicle->angularVelocity.z) : Vector3::zero();
    }

    Vector3 Vehicle::GetRotationDegrees() const {
        auto *vehicle = ResolveVehicle();

        if (!vehicle) {
            return Vector3::zero();
        }

        const auto degrees = Shared::MtaDegreesFromQuat(vehicle->rotation);
        return Vector3(degrees.x, degrees.y, degrees.z);
    }

    v8::Local<v8::Object> Vehicle::Wrap(v8::Isolate *isolate, std::uint64_t networkId) {
        GetClass(isolate);
        return v8pp::class_<Vehicle>::create_object(isolate, networkId);
    }

    void Vehicle::JS_GetController(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        auto *self    = v8pp::class_<Vehicle>::unwrap_object(isolate, info.This());
        auto *vehicle = self ? self->ResolveVehicle() : nullptr;
        auto *rep     = Framework::CoreModules::GetReplication();

        if (!vehicle || !rep) {
            info.GetReturnValue().SetNull();
            return;
        }

        const auto vehicleId = static_cast<std::uint64_t>(vehicle->GetNetworkID());
        std::uint64_t driver = 0;

        rep->ForEachEntity([&](Framework::Networking::Replication::NetworkEntity *entity) {
            auto *player = dynamic_cast<Shared::PlayerEntity *>(entity);

            if (player && player->vehicleNetworkId == vehicleId && player->vehicleSeat == 0) {
                driver = static_cast<std::uint64_t>(player->GetNetworkID());
            }
        });

        if (driver == 0) {
            info.GetReturnValue().SetNull();
            return;
        }

        info.GetReturnValue().Set(Player::Wrap(isolate, driver));
    }

    void Vehicle::JS_GetOccupants(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        auto  ctx     = isolate->GetCurrentContext();
        auto *self    = v8pp::class_<Vehicle>::unwrap_object(isolate, info.This());
        auto *vehicle = self ? self->ResolveVehicle() : nullptr;
        auto *rep     = Framework::CoreModules::GetReplication();
        auto  result  = v8::Object::New(isolate);

        if (!vehicle || !rep) {
            info.GetReturnValue().Set(result);
            return;
        }

        const auto vehicleId = static_cast<std::uint64_t>(vehicle->GetNetworkID());

        rep->ForEachEntity([&](Framework::Networking::Replication::NetworkEntity *entity) {
            auto *player = dynamic_cast<Shared::PlayerEntity *>(entity);

            if (player && player->vehicleNetworkId == vehicleId) {
                result->Set(ctx, v8pp::to_v8(isolate, player->vehicleSeat),
                            Player::Wrap(isolate, static_cast<std::uint64_t>(player->GetNetworkID())))
                    .Check();
            }
        });

        info.GetReturnValue().Set(result);
    }

    void Vehicle::JS_GetOccupant(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        auto *self    = v8pp::class_<Vehicle>::unwrap_object(isolate, info.This());
        auto *vehicle = self ? self->ResolveVehicle() : nullptr;
        auto *rep     = Framework::CoreModules::GetReplication();
        const auto seat = info.Length() >= 1 ? v8pp::from_v8<std::uint8_t>(isolate, info[0]) : std::uint8_t {0};

        if (!vehicle || !rep) {
            info.GetReturnValue().SetNull();
            return;
        }

        const auto vehicleId = static_cast<std::uint64_t>(vehicle->GetNetworkID());
        std::uint64_t found  = 0;

        rep->ForEachEntity([&](Framework::Networking::Replication::NetworkEntity *entity) {
            auto *player = dynamic_cast<Shared::PlayerEntity *>(entity);

            if (player && player->vehicleNetworkId == vehicleId && player->vehicleSeat == seat) {
                found = static_cast<std::uint64_t>(player->GetNetworkID());
            }
        });

        if (found == 0) {
            info.GetReturnValue().SetNull();
            return;
        }

        info.GetReturnValue().Set(Player::Wrap(isolate, found));
    }

    v8pp::class_<Vehicle> &Vehicle::GetClass(v8::Isolate *isolate) {
        auto found = gClasses.find(isolate);

        if (found != gClasses.end()) {
            return *found->second;
        }

        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        Color::GetClass(isolate);
        Vector3::GetClass(isolate);

        auto &cls = gClasses[isolate];
        cls       = std::make_unique<v8pp::class_<Vehicle>>(isolate, Framework::Scripting::GetScriptingCatalog(isolate), "Vehicle",
                                                     "A streamed GTA vehicle on this client. Readonly replica fields.");

        cls->inherit<Framework::Scripting::Builtins::Entity>()
            .document_base("Entity")
            .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity id.")},
                                                      "Wraps an existing streamed vehicle; does not create one."))
            .property("model", &Vehicle::GetModel, v8pp::metadata::property_docs("number", "Vehicle model id."))
            .property("color", &Vehicle::GetColor1, v8pp::metadata::property_docs("Color", "Primary body colour."))
            .property("color2", &Vehicle::GetColor2, v8pp::metadata::property_docs("Color", "Secondary body colour."))
            .property("color3", &Vehicle::GetColor3, v8pp::metadata::property_docs("Color", "Third body colour."))
            .property("color4", &Vehicle::GetColor4, v8pp::metadata::property_docs("Color", "Fourth body colour."))
            .property("paintjob", &Vehicle::GetPaintjob, v8pp::metadata::property_docs("number", "Paintjob id."))
            .property("health", &Vehicle::GetHealth, v8pp::metadata::property_docs("number", "Vehicle health."))
            .property("engine", &Vehicle::GetEngine, v8pp::metadata::property_docs("boolean", "Engine on/off."))
            .property("siren", &Vehicle::GetSiren, v8pp::metadata::property_docs("boolean", "Siren on/off."))
            .property("locked", &Vehicle::GetLocked, v8pp::metadata::property_docs("boolean", "Doors locked."))
            .property("blown", &Vehicle::GetBlown, v8pp::metadata::property_docs("boolean", "Whether the vehicle is blown up."))
            .property("plateText", &Vehicle::GetPlateText, v8pp::metadata::property_docs("string", "Number plate text."))
            .property("dirtLevel", &Vehicle::GetDirtLevel, v8pp::metadata::property_docs("number", "Dirt level."))
            .property("taxiLight", &Vehicle::GetTaxiLight, v8pp::metadata::property_docs("boolean", "Taxi light on/off."))
            .property("frozen", &Vehicle::GetFrozen, v8pp::metadata::property_docs("boolean", "Whether the vehicle is frozen."))
            .property("damageProof", &Vehicle::GetDamageProof, v8pp::metadata::property_docs("boolean", "Whether the vehicle is damage-proof."))
            .property("collisions", &Vehicle::GetCollisions, v8pp::metadata::property_docs("boolean", "Whether collisions are enabled."))
            .property("landingGearDown", &Vehicle::GetLandingGearDown, v8pp::metadata::property_docs("boolean", "Landing gear state."))
            .property("lights", &Vehicle::GetLights, v8pp::metadata::property_docs("boolean", "Headlights on/off."))
            .property("alpha", &Vehicle::GetAlpha, v8pp::metadata::property_docs("number", "Vehicle alpha 0..255."))
            .property("onGround", &Vehicle::IsOnGround, v8pp::metadata::property_docs("boolean", "Whether the vehicle is on the ground."))
            .property("engineBroken", &Vehicle::GetEngineBroken, v8pp::metadata::property_docs("boolean", "Whether the engine is broken."))
            .property("interior", &Vehicle::GetInterior, v8pp::metadata::property_docs("number", "Interior id."))
            .property("dimension", &Vehicle::GetDimension, v8pp::metadata::property_docs("number", "Dimension id."))
            .property("velocity", &Vehicle::GetVelocity, v8pp::metadata::property_docs("Vector3", "Linear velocity."))
            .property("turnVelocity", &Vehicle::GetTurnVelocity, v8pp::metadata::property_docs("Vector3", "Angular velocity."))
            .property("rotation", &Vehicle::GetRotationDegrees, v8pp::metadata::property_docs("Vector3", "MTA degrees X, Y, Z."));

        cls->prototype_function("getController", &Vehicle::JS_GetController,
                                v8pp::metadata::docs("Player | null", {}, "Returns the driver, if any.", "Driver handle or null."));
        cls->prototype_function("getOccupants", &Vehicle::JS_GetOccupants,
                                v8pp::metadata::docs("Record<number, Player>", {}, "Returns a sparse map of seat -> Player.", "Occupant map."));
        cls->prototype_function("getOccupant", &Vehicle::JS_GetOccupant,
                                v8pp::metadata::docs("Player | null", {v8pp::metadata::param("seat", "number", true, "Seat index (0 = driver).")},
                                                     "Returns the player in a seat, if any.", "Player handle or null."));

        return *cls;
    }

    void Vehicle::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &cls  = GetClass(isolate);
        auto  ctx  = isolate->GetCurrentContext();
        auto  ctor = cls.js_function_template()->GetFunction(ctx).ToLocalChecked();

        global->Set(ctx, v8pp::to_v8(isolate, "Vehicle"), ctor).Check();
    }
} // namespace VantaMP::Scripting
