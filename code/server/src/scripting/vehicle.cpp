#include "vehicle.h"

#include "game/vehicle_manager.h"
#include "shared/catalog/model_catalog.h"
#include "shared/vehicle/colors.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/vehicle/upgrades.h"

#include <scripting/builtins/color.h>
#include <scripting/builtins/entity.h>
#include <scripting/builtins/vector3.h>
#include <scripting/scripting_catalog.h>
#include <v8pp/convert.hpp>

#include <algorithm>
#include <cstdint>

namespace VantaMP::Scripting {
    namespace {
        using Framework::Scripting::Builtins::Color;
        using Framework::Scripting::Builtins::Vector3;

        std::uint32_t PackColor(const Color &color) {
            return Shared::PackRgb(Shared::RgbByte(color.getR()), Shared::RgbByte(color.getG()), Shared::RgbByte(color.getB()));
        }

        Color UnpackColor(std::uint32_t packed) {
            return Color::fromRGB(static_cast<float>(Shared::RgbR(packed)), static_cast<float>(Shared::RgbG(packed)),
                                  static_cast<float>(Shared::RgbB(packed)));
        }

        bool ReadColorValue(v8::Isolate *isolate, v8::Local<v8::Value> value, std::uint32_t &out) {
            if (auto *color = v8pp::class_<Color>::unwrap_object(isolate, value)) {
                out = PackColor(*color);
                return true;
            }

            if (value->IsNumber()) {
                out = Shared::PaletteRgb(v8pp::from_v8<std::uint8_t>(isolate, value));
                return true;
            }

            return false;
        }

        bool ReadRgbTriple(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &info, int start, std::uint32_t &out) {
            if (info.Length() < start + 3 || !info[start]->IsNumber() || !info[start + 1]->IsNumber() || !info[start + 2]->IsNumber()) {
                return false;
            }

            const auto r = static_cast<std::uint8_t>(std::clamp(v8pp::from_v8<int>(isolate, info[start]), 0, 255));
            const auto g = static_cast<std::uint8_t>(std::clamp(v8pp::from_v8<int>(isolate, info[start + 1]), 0, 255));
            const auto b = static_cast<std::uint8_t>(std::clamp(v8pp::from_v8<int>(isolate, info[start + 2]), 0, 255));

            out = Shared::PackRgb(r, g, b);
            return true;
        }

        Color ColorOf(const Shared::VehicleEntity *vehicle, int slot) {
            if (!vehicle || slot < 0 || slot >= Shared::kVehicleColorCount) {
                return Color::black();
            }

            return UnpackColor(vehicle->colours[static_cast<std::size_t>(slot)]);
        }

        void WriteSlot(Game::VehicleManager *manager, Shared::VehicleEntity &vehicle, int slot, std::uint32_t packed) {
            if (!manager || slot < 0 || slot >= Shared::kVehicleColorCount) {
                return;
            }

            auto colours = vehicle.colours;
            colours[static_cast<std::size_t>(slot)] = packed;
            manager->SetColors(vehicle, colours[0], colours[1], colours[2], colours[3]);
        }
    } // namespace

    Vehicle::Vehicle(std::uint64_t networkId): Entity(networkId) {}

    Shared::VehicleEntity *Vehicle::ResolveVehicle() const {
        return dynamic_cast<Shared::VehicleEntity *>(GetHandle());
    }

    std::uint32_t Vehicle::GetModel() const {
        auto *vehicle = ResolveVehicle();

        return vehicle ? vehicle->modelId : 0;
    }

    Color Vehicle::GetColor1() const {
        return ColorOf(ResolveVehicle(), 0);
    }

    void Vehicle::SetColor1(const Color &color) {
        if (auto *vehicle = ResolveVehicle()) {
            WriteSlot(gVehicles, *vehicle, 0, PackColor(color));
        }
    }

    Color Vehicle::GetColor2() const {
        return ColorOf(ResolveVehicle(), 1);
    }

    void Vehicle::SetColor2(const Color &color) {
        if (auto *vehicle = ResolveVehicle()) {
            WriteSlot(gVehicles, *vehicle, 1, PackColor(color));
        }
    }

    Color Vehicle::GetColor3() const {
        return ColorOf(ResolveVehicle(), 2);
    }

    void Vehicle::SetColor3(const Color &color) {
        if (auto *vehicle = ResolveVehicle()) {
            WriteSlot(gVehicles, *vehicle, 2, PackColor(color));
        }
    }

    Color Vehicle::GetColor4() const {
        return ColorOf(ResolveVehicle(), 3);
    }

    void Vehicle::SetColor4(const Color &color) {
        if (auto *vehicle = ResolveVehicle()) {
            WriteSlot(gVehicles, *vehicle, 3, PackColor(color));
        }
    }

    std::uint8_t Vehicle::GetPaintjob() const {
        auto *vehicle = ResolveVehicle();

        return vehicle ? vehicle->paintjob : Shared::kPaintjobNone;
    }

    void Vehicle::SetPaintjob(std::uint8_t paintjob) {
        if (auto *vehicle = ResolveVehicle(); vehicle && gVehicles) {
            gVehicles->SetPaintjob(*vehicle, paintjob);
        }
    }

    std::uint8_t Vehicle::GetInterior() const {
        auto *vehicle = ResolveVehicle();

        return vehicle ? vehicle->interior : 0;
    }

    void Vehicle::SetInterior(std::uint8_t interior) {
        if (auto *vehicle = ResolveVehicle(); vehicle && gVehicles) {
            gVehicles->SetInterior(*vehicle, interior);
        }
    }

    std::uint16_t Vehicle::GetDimension() const {
        auto *vehicle = ResolveVehicle();

        return vehicle ? vehicle->dimension : 0;
    }

    void Vehicle::SetDimension(std::uint16_t dimension) {
        if (auto *vehicle = ResolveVehicle(); vehicle && gVehicles) {
            gVehicles->SetDimension(*vehicle, dimension);
        }
    }

    bool Vehicle::AddUpgrade(std::uint16_t upgradeId) {
        auto *vehicle = ResolveVehicle();

        return vehicle && gVehicles && gVehicles->AddUpgrade(*vehicle, upgradeId);
    }

    bool Vehicle::RemoveUpgrade(std::uint16_t upgradeId) {
        auto *vehicle = ResolveVehicle();

        return vehicle && gVehicles && gVehicles->RemoveUpgrade(*vehicle, upgradeId);
    }

    void Vehicle::RemoveAllUpgrades() {
        if (auto *vehicle = ResolveVehicle(); vehicle && gVehicles) {
            gVehicles->RemoveAllUpgrades(*vehicle);
        }
    }

    void Vehicle::Destroy() {
        if (gVehicles) {
            gVehicles->Destroy(_id);
        }
    }

    void Vehicle::Bind(Game::VehicleManager &vehicles) {
        gVehicles = &vehicles;
    }

    v8::Local<v8::Object> Vehicle::Wrap(v8::Isolate *isolate, std::uint64_t networkId) {
        GetClass(isolate);
        return v8pp::class_<Vehicle>::create_object(isolate, networkId);
    }

    void Vehicle::Create(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();

        if (!gVehicles || info.Length() < 1) {
            return;
        }

        const auto model = v8pp::from_v8<std::uint32_t>(isolate, info[0]);

        if (!Shared::ModelCatalog::IsVehicle(model)) {
            return;
        }

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        int arg = 1;

        if (info.Length() > 1) {
            if (auto *pos = v8pp::class_<Vector3>::unwrap_object(isolate, info[1])) {
                x   = pos->getX();
                y   = pos->getY();
                z   = pos->getZ();
                arg = 2;
            }
            else if (info.Length() >= 4) {
                x   = v8pp::from_v8<float>(isolate, info[1]);
                y   = v8pp::from_v8<float>(isolate, info[2]);
                z   = v8pp::from_v8<float>(isolate, info[3]);
                arg = 4;
            }
        }

        const auto heading = info.Length() > arg ? v8pp::from_v8<float>(isolate, info[arg]) : 0.0f;
        std::uint8_t primary   = 0;
        std::uint8_t secondary = 0;
        std::uint32_t colour1  = Shared::PaletteRgb(0);
        std::uint32_t colour2  = Shared::PaletteRgb(0);
        bool haveColour        = false;

        if (info.Length() > arg + 1 && ReadColorValue(isolate, info[arg + 1], colour1)) {
            haveColour = true;
            if (info[arg + 1]->IsNumber()) {
                primary = v8pp::from_v8<std::uint8_t>(isolate, info[arg + 1]);
            }
        }

        if (info.Length() > arg + 2 && ReadColorValue(isolate, info[arg + 2], colour2)) {
            haveColour = true;
            if (info[arg + 2]->IsNumber()) {
                secondary = v8pp::from_v8<std::uint8_t>(isolate, info[arg + 2]);
            }
        }

        const auto interior  = info.Length() > arg + 3 ? v8pp::from_v8<std::uint8_t>(isolate, info[arg + 3]) : std::uint8_t {0};
        const auto dimension = info.Length() > arg + 4 ? v8pp::from_v8<std::uint16_t>(isolate, info[arg + 4]) : std::uint16_t {0};

        const auto id = gVehicles->Create(model, x, y, z, heading, primary, secondary, interior, dimension);

        if (id == 0) {
            return;
        }

        if (auto *vehicle = gVehicles->Find(id)) {
            if (haveColour) {
                vehicle->SetColours(colour1, colour2, colour1, colour2);
            }
        }

        info.GetReturnValue().Set(Wrap(isolate, id));
    }

    void Vehicle::JS_SetColor(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        auto *self    = v8pp::class_<Vehicle>::unwrap_object(isolate, info.This());
        auto *vehicle = self ? self->ResolveVehicle() : nullptr;

        if (!vehicle || !gVehicles || info.Length() < 1) {
            return;
        }

        auto colours = vehicle->colours;

        if (v8pp::class_<Color>::unwrap_object(isolate, info[0]) != nullptr) {
            if (!ReadColorValue(isolate, info[0], colours[0])) {
                return;
            }

            if (info.Length() > 1) {
                ReadColorValue(isolate, info[1], colours[1]);
            }
            else {
                colours[1] = colours[0];
            }

            if (info.Length() > 2) {
                ReadColorValue(isolate, info[2], colours[2]);
            }
            else {
                colours[2] = colours[0];
            }

            if (info.Length() > 3) {
                ReadColorValue(isolate, info[3], colours[3]);
            }
            else {
                colours[3] = colours[1];
            }
        }
        else if (info.Length() >= 3 && info[0]->IsNumber() && info[1]->IsNumber() && info[2]->IsNumber()) {
            if (!ReadRgbTriple(isolate, info, 0, colours[0])) {
                return;
            }

            if (info.Length() >= 6) {
                ReadRgbTriple(isolate, info, 3, colours[1]);
            }
            else {
                colours[1] = colours[0];
            }

            if (info.Length() >= 9) {
                ReadRgbTriple(isolate, info, 6, colours[2]);
            }
            else {
                colours[2] = colours[0];
            }

            if (info.Length() >= 12) {
                ReadRgbTriple(isolate, info, 9, colours[3]);
            }
            else {
                colours[3] = colours[1];
            }
        }
        else if (ReadColorValue(isolate, info[0], colours[0])) {
            if (info.Length() > 1) {
                ReadColorValue(isolate, info[1], colours[1]);
            }
            else {
                colours[1] = colours[0];
            }

            colours[2] = colours[0];
            colours[3] = colours[1];
        }
        else {
            return;
        }

        gVehicles->SetColors(*vehicle, colours[0], colours[1], colours[2], colours[3]);
    }

    v8pp::class_<Vehicle> &Vehicle::GetClass(v8::Isolate *isolate) {
        auto found = gClasses.find(isolate);

        if (found != gClasses.end()) {
            return *found->second;
        }

        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        Color::GetClass(isolate);

        auto &cls = gClasses[isolate];
        cls       = std::make_unique<v8pp::class_<Vehicle>>(isolate, Framework::Scripting::GetScriptingCatalog(isolate), "Vehicle",
                                                     "A GTA vehicle. Position and rotation are the Framework Entity ones. "
                                                     "Colour is any RGB, not a palette index.");

        cls->inherit<Framework::Scripting::Builtins::Entity>()
            .ctor<std::uint64_t>()
            .function("destroy", &Vehicle::Destroy)
            .function("addUpgrade", &Vehicle::AddUpgrade)
            .function("removeUpgrade", &Vehicle::RemoveUpgrade)
            .function("removeAllUpgrades", &Vehicle::RemoveAllUpgrades)
            .property("model", &Vehicle::GetModel)
            .property("color", &Vehicle::GetColor1, &Vehicle::SetColor1)
            .property("color2", &Vehicle::GetColor2, &Vehicle::SetColor2)
            .property("color3", &Vehicle::GetColor3, &Vehicle::SetColor3)
            .property("color4", &Vehicle::GetColor4, &Vehicle::SetColor4)
            .property("paintjob", &Vehicle::GetPaintjob, &Vehicle::SetPaintjob)
            .property("interior", &Vehicle::GetInterior, &Vehicle::SetInterior)
            .property("dimension", &Vehicle::GetDimension, &Vehicle::SetDimension);

        cls->prototype_function("setColor", &Vehicle::JS_SetColor);

        return *cls;
    }

    void Vehicle::DestroyById(const v8::FunctionCallbackInfo<v8::Value> &info) {
        if (!gVehicles || info.Length() < 1) {
            return;
        }

        gVehicles->Destroy(v8pp::from_v8<std::uint64_t>(info.GetIsolate(), info[0]));
    }

    void Vehicle::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &cls  = GetClass(isolate);
        auto  ctx  = isolate->GetCurrentContext();
        auto  ctor = cls.js_function_template()->GetFunction(ctx).ToLocalChecked();

        v8::Local<v8::Function> create = v8::FunctionTemplate::New(isolate, &Vehicle::Create)->GetFunction(ctx).ToLocalChecked();
        ctor.As<v8::Object>()->Set(ctx, v8pp::to_v8(isolate, "create"), create).Check();
        global->Set(ctx, v8pp::to_v8(isolate, "Vehicle"), ctor).Check();

        auto vehicles = v8::Object::New(isolate);

        vehicles->Set(ctx, v8pp::to_v8(isolate, "create"), create).Check();
        vehicles->Set(ctx, v8pp::to_v8(isolate, "destroy"), v8::FunctionTemplate::New(isolate, &Vehicle::DestroyById)->GetFunction(ctx).ToLocalChecked()).Check();
        vehicles->Set(ctx, v8pp::to_v8(isolate, "setInterior"),
                      v8::FunctionTemplate::New(isolate,
                                                [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                                                    if (!gVehicles || info.Length() < 2) {
                                                        return;
                                                    }

                                                    auto *isolate = info.GetIsolate();
                                                    auto *vehicle = gVehicles->Find(v8pp::from_v8<std::uint64_t>(isolate, info[0]));

                                                    if (vehicle) {
                                                        gVehicles->SetInterior(*vehicle, v8pp::from_v8<std::uint8_t>(isolate, info[1]));
                                                    }
                                                })
                          ->GetFunction(ctx)
                          .ToLocalChecked())
            .Check();
        vehicles->Set(ctx, v8pp::to_v8(isolate, "setDimension"),
                      v8::FunctionTemplate::New(isolate,
                                                [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                                                    if (!gVehicles || info.Length() < 2) {
                                                        return;
                                                    }

                                                    auto *isolate = info.GetIsolate();
                                                    auto *vehicle = gVehicles->Find(v8pp::from_v8<std::uint64_t>(isolate, info[0]));

                                                    if (vehicle) {
                                                        gVehicles->SetDimension(*vehicle, v8pp::from_v8<std::uint16_t>(isolate, info[1]));
                                                    }
                                                })
                          ->GetFunction(ctx)
                          .ToLocalChecked())
            .Check();
        vehicles->Set(ctx, v8pp::to_v8(isolate, "getInterior"),
                      v8::FunctionTemplate::New(isolate,
                                                [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                                                    auto *isolate = info.GetIsolate();
                                                    auto *vehicle = gVehicles && info.Length() >= 1 ? gVehicles->Find(v8pp::from_v8<std::uint64_t>(isolate, info[0])) : nullptr;

                                                    info.GetReturnValue().Set(v8::Integer::New(isolate, vehicle ? vehicle->interior : 0));
                                                })
                          ->GetFunction(ctx)
                          .ToLocalChecked())
            .Check();
        vehicles->Set(ctx, v8pp::to_v8(isolate, "getDimension"),
                      v8::FunctionTemplate::New(isolate,
                                                [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                                                    auto *isolate = info.GetIsolate();
                                                    auto *vehicle = gVehicles && info.Length() >= 1 ? gVehicles->Find(v8pp::from_v8<std::uint64_t>(isolate, info[0])) : nullptr;

                                                    info.GetReturnValue().Set(v8::Integer::New(isolate, vehicle ? vehicle->dimension : 0));
                                                })
                          ->GetFunction(ctx)
                          .ToLocalChecked())
            .Check();
        global->Set(ctx, v8pp::to_v8(isolate, "Vehicles"), vehicles).Check();
    }
} // namespace VantaMP::Scripting
