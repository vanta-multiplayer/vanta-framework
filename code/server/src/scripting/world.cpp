#include "world.h"

#include "game/world_manager.h"
#include "shared/rpc/world_env.h"

#include <scripting/scripting_catalog.h>
#include <v8pp/convert.hpp>
#include <v8pp/metadata.hpp>

namespace VantaMP::Scripting {
    void World::Bind(Game::WorldManager &world) {
        gWorld = &world;
    }

    std::uint8_t World::GetHour() {
        std::uint8_t hour = 12, minute = 0;

        if (gWorld) {
            gWorld->GetTime(hour, minute);
        }

        return hour;
    }

    void World::SetHour(std::uint8_t hour) {
        if (!gWorld) {
            return;
        }

        std::uint8_t h = 12, m = 0;
        gWorld->GetTime(h, m);
        gWorld->SetTime(hour, m);
    }

    std::uint8_t World::GetMinute() {
        std::uint8_t hour = 12, minute = 0;

        if (gWorld) {
            gWorld->GetTime(hour, minute);
        }

        return minute;
    }

    void World::SetMinute(std::uint8_t minute) {
        if (!gWorld) {
            return;
        }

        std::uint8_t h = 12, m = 0;
        gWorld->GetTime(h, m);
        gWorld->SetTime(h, minute);
    }

    void World::SetTime(std::uint8_t hour, std::uint8_t minute) {
        if (gWorld) {
            gWorld->SetTime(hour, minute);
        }
    }

    std::uint32_t World::GetMinuteDuration() {
        return gWorld ? gWorld->GetMinuteDuration() : Shared::kDefaultMinuteDuration;
    }

    void World::SetMinuteDuration(std::uint32_t ms) {
        if (gWorld) {
            gWorld->SetMinuteDuration(ms);
        }
    }

    float World::GetGameSpeed() {
        return gWorld ? gWorld->GetGameSpeed() : Shared::kDefaultGameSpeed;
    }

    void World::SetGameSpeed(float speed) {
        if (gWorld) {
            gWorld->SetGameSpeed(speed);
        }
    }

    std::uint8_t World::GetWeather() {
        return gWorld ? gWorld->GetWeather() : 0;
    }

    void World::SetWeather(std::uint8_t weather) {
        if (gWorld) {
            gWorld->SetWeather(weather);
        }
    }

    float World::GetGravity() {
        return gWorld ? gWorld->GetGravity() : Shared::kDefaultGravity;
    }

    void World::SetGravity(float gravity) {
        if (gWorld) {
            gWorld->SetGravity(gravity);
        }
    }

    float World::GetFarClip() {
        return gWorld ? gWorld->GetFarClip() : 0.0f;
    }

    void World::SetFarClip(float distance) {
        if (gWorld) {
            gWorld->SetFarClip(distance);
        }
    }

    float World::GetFogDistance() {
        return gWorld ? gWorld->GetFogDistance() : 0.0f;
    }

    void World::SetFogDistance(float distance) {
        if (gWorld) {
            gWorld->SetFogDistance(distance);
        }
    }

    float World::GetRainLevel() {
        return gWorld ? gWorld->GetRainLevel() : 0.0f;
    }

    void World::SetRainLevel(float level) {
        if (gWorld) {
            gWorld->SetRainLevel(level);
        }
    }

    float World::GetWaveHeight() {
        return gWorld ? gWorld->GetWaveHeight() : 0.0f;
    }

    void World::SetWaveHeight(float height) {
        if (gWorld) {
            gWorld->SetWaveHeight(height);
        }
    }

    std::uint8_t World::GetMoonSize() {
        return gWorld ? gWorld->GetMoonSize() : Shared::kDefaultMoonSize;
    }

    void World::SetMoonSize(std::uint8_t size) {
        if (gWorld) {
            gWorld->SetMoonSize(size);
        }
    }

    float World::GetSunSize() {
        return gWorld ? gWorld->GetSunSize() : 1.0f;
    }

    void World::SetSunSize(float size) {
        if (gWorld) {
            gWorld->SetSunSize(size);
        }
    }

    void World::JS_SetTime(const v8::FunctionCallbackInfo<v8::Value> &info) {
        if (info.Length() < 2) {
            return;
        }

        auto *isolate = info.GetIsolate();
        SetTime(v8pp::from_v8<std::uint8_t>(isolate, info[0]), v8pp::from_v8<std::uint8_t>(isolate, info[1]));
    }

    void World::JS_SetSkyGradient(const v8::FunctionCallbackInfo<v8::Value> &info) {
        if (!gWorld || info.Length() < 6) {
            return;
        }

        auto *isolate = info.GetIsolate();
        gWorld->SetSkyGradient(v8pp::from_v8<std::uint8_t>(isolate, info[0]), v8pp::from_v8<std::uint8_t>(isolate, info[1]),
                               v8pp::from_v8<std::uint8_t>(isolate, info[2]), v8pp::from_v8<std::uint8_t>(isolate, info[3]),
                               v8pp::from_v8<std::uint8_t>(isolate, info[4]), v8pp::from_v8<std::uint8_t>(isolate, info[5]));
    }

    void World::JS_GetSkyGradient(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        auto  ctx     = isolate->GetCurrentContext();
        auto  arr     = v8::Array::New(isolate, 6);

        std::uint8_t topR = 0, topG = 0, topB = 0, botR = 0, botG = 0, botB = 0;
        if (gWorld) {
            gWorld->GetSkyGradient(topR, topG, topB, botR, botG, botB);
        }

        arr->Set(ctx, 0, v8pp::to_v8(isolate, topR)).Check();
        arr->Set(ctx, 1, v8pp::to_v8(isolate, topG)).Check();
        arr->Set(ctx, 2, v8pp::to_v8(isolate, topB)).Check();
        arr->Set(ctx, 3, v8pp::to_v8(isolate, botR)).Check();
        arr->Set(ctx, 4, v8pp::to_v8(isolate, botG)).Check();
        arr->Set(ctx, 5, v8pp::to_v8(isolate, botB)).Check();
        info.GetReturnValue().Set(arr);
    }

    void World::JS_SetSunColor(const v8::FunctionCallbackInfo<v8::Value> &info) {
        if (!gWorld || info.Length() < 6) {
            return;
        }

        auto *isolate = info.GetIsolate();
        gWorld->SetSunColor(v8pp::from_v8<std::uint8_t>(isolate, info[0]), v8pp::from_v8<std::uint8_t>(isolate, info[1]),
                            v8pp::from_v8<std::uint8_t>(isolate, info[2]), v8pp::from_v8<std::uint8_t>(isolate, info[3]),
                            v8pp::from_v8<std::uint8_t>(isolate, info[4]), v8pp::from_v8<std::uint8_t>(isolate, info[5]));
    }

    void World::JS_GetSunColor(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        auto  ctx     = isolate->GetCurrentContext();
        auto  arr     = v8::Array::New(isolate, 6);

        std::uint8_t coreR = 0, coreG = 0, coreB = 0, coronaR = 0, coronaG = 0, coronaB = 0;
        if (gWorld) {
            gWorld->GetSunColor(coreR, coreG, coreB, coronaR, coronaG, coronaB);
        }

        arr->Set(ctx, 0, v8pp::to_v8(isolate, coreR)).Check();
        arr->Set(ctx, 1, v8pp::to_v8(isolate, coreG)).Check();
        arr->Set(ctx, 2, v8pp::to_v8(isolate, coreB)).Check();
        arr->Set(ctx, 3, v8pp::to_v8(isolate, coronaR)).Check();
        arr->Set(ctx, 4, v8pp::to_v8(isolate, coronaG)).Check();
        arr->Set(ctx, 5, v8pp::to_v8(isolate, coronaB)).Check();
        info.GetReturnValue().Set(arr);
    }

    void World::JS_SetWaterColor(const v8::FunctionCallbackInfo<v8::Value> &info) {
        if (!gWorld || info.Length() < 3) {
            return;
        }

        auto *isolate = info.GetIsolate();
        const float a = info.Length() > 3 ? v8pp::from_v8<float>(isolate, info[3]) : 255.0f;
        gWorld->SetWaterColor(v8pp::from_v8<float>(isolate, info[0]), v8pp::from_v8<float>(isolate, info[1]), v8pp::from_v8<float>(isolate, info[2]), a);
    }

    void World::JS_GetWaterColor(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        auto  ctx     = isolate->GetCurrentContext();
        auto  arr     = v8::Array::New(isolate, 4);

        float r = 0, g = 0, b = 0, a = 255;
        if (gWorld) {
            gWorld->GetWaterColor(r, g, b, a);
        }

        arr->Set(ctx, 0, v8pp::to_v8(isolate, r)).Check();
        arr->Set(ctx, 1, v8pp::to_v8(isolate, g)).Check();
        arr->Set(ctx, 2, v8pp::to_v8(isolate, b)).Check();
        arr->Set(ctx, 3, v8pp::to_v8(isolate, a)).Check();
        info.GetReturnValue().Set(arr);
    }

    void World::JS_SetHeatHaze(const v8::FunctionCallbackInfo<v8::Value> &info) {
        if (!gWorld || info.Length() < 1) {
            return;
        }

        auto *isolate = info.GetIsolate();
        const auto intensity   = v8pp::from_v8<std::uint8_t>(isolate, info[0]);
        const auto randomShift = info.Length() > 1 ? v8pp::from_v8<std::uint8_t>(isolate, info[1]) : std::uint8_t {0};
        const auto speedMin    = info.Length() > 2 ? v8pp::from_v8<std::uint16_t>(isolate, info[2]) : std::uint16_t {12};
        const auto speedMax    = info.Length() > 3 ? v8pp::from_v8<std::uint16_t>(isolate, info[3]) : std::uint16_t {18};
        const auto scanX       = info.Length() > 4 ? v8pp::from_v8<std::int16_t>(isolate, info[4]) : std::int16_t {75};
        const auto scanY       = info.Length() > 5 ? v8pp::from_v8<std::int16_t>(isolate, info[5]) : std::int16_t {80};
        const auto renderX     = info.Length() > 6 ? v8pp::from_v8<std::uint16_t>(isolate, info[6]) : std::uint16_t {80};
        const auto renderY     = info.Length() > 7 ? v8pp::from_v8<std::uint16_t>(isolate, info[7]) : std::uint16_t {85};
        const auto inside      = info.Length() > 8 && info[8]->BooleanValue(isolate);

        gWorld->SetHeatHaze(intensity, randomShift, speedMin, speedMax, scanX, scanY, renderX, renderY, inside);
    }

    void World::JS_GetHeatHaze(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        auto  ctx     = isolate->GetCurrentContext();
        auto  arr     = v8::Array::New(isolate, 9);

        std::uint8_t intensity = 0, randomShift = 0;
        std::uint16_t speedMin = 12, speedMax = 18, renderX = 80, renderY = 85;
        std::int16_t scanX = 75, scanY = 80;
        bool inside = false;

        if (gWorld) {
            gWorld->GetHeatHaze(intensity, randomShift, speedMin, speedMax, scanX, scanY, renderX, renderY, inside);
        }

        arr->Set(ctx, 0, v8pp::to_v8(isolate, intensity)).Check();
        arr->Set(ctx, 1, v8pp::to_v8(isolate, randomShift)).Check();
        arr->Set(ctx, 2, v8pp::to_v8(isolate, speedMin)).Check();
        arr->Set(ctx, 3, v8pp::to_v8(isolate, speedMax)).Check();
        arr->Set(ctx, 4, v8pp::to_v8(isolate, scanX)).Check();
        arr->Set(ctx, 5, v8pp::to_v8(isolate, scanY)).Check();
        arr->Set(ctx, 6, v8pp::to_v8(isolate, renderX)).Check();
        arr->Set(ctx, 7, v8pp::to_v8(isolate, renderY)).Check();
        arr->Set(ctx, 8, v8::Boolean::New(isolate, inside)).Check();
        info.GetReturnValue().Set(arr);
    }

    bool World::GetCloudsEnabled() {
        return gWorld ? gWorld->GetCloudsEnabled() : true;
    }

    void World::SetCloudsEnabled(bool enabled) {
        if (gWorld) {
            gWorld->SetCloudsEnabled(enabled);
        }
    }

    void World::JS_SetCloudsEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
        if (info.Length() < 1) {
            return;
        }

        SetCloudsEnabled(info[0]->BooleanValue(info.GetIsolate()));
    }

    float World::GetJetpackMaxHeight() {
        return gWorld ? gWorld->GetJetpackMaxHeight() : Shared::kDefaultJetpackHeight;
    }

    void World::SetJetpackMaxHeight(float height) {
        if (gWorld) {
            gWorld->SetJetpackMaxHeight(height);
        }
    }

    void World::JS_SetWindVelocity(const v8::FunctionCallbackInfo<v8::Value> &info) {
        if (!gWorld || info.Length() < 3) {
            return;
        }

        auto *isolate = info.GetIsolate();
        gWorld->SetWindVelocity(v8pp::from_v8<float>(isolate, info[0]), v8pp::from_v8<float>(isolate, info[1]), v8pp::from_v8<float>(isolate, info[2]));
    }

    void World::JS_GetWindVelocity(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        auto  ctx     = isolate->GetCurrentContext();
        auto  arr     = v8::Array::New(isolate, 3);

        float x = 0, y = 0, z = 0;
        if (gWorld) {
            gWorld->GetWindVelocity(x, y, z);
        }

        arr->Set(ctx, 0, v8pp::to_v8(isolate, x)).Check();
        arr->Set(ctx, 1, v8pp::to_v8(isolate, y)).Check();
        arr->Set(ctx, 2, v8pp::to_v8(isolate, z)).Check();
        info.GetReturnValue().Set(arr);
    }

    namespace {
        template <auto Getter>
        void JS_Get(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(v8pp::to_v8(info.GetIsolate(), Getter()));
        }

        template <auto Setter, typename T>
        void JS_Set(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() < 1) {
                return;
            }

            Setter(v8pp::from_v8<T>(info.GetIsolate(), info[0]));
        }
    } // namespace

    void World::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto ctx   = isolate->GetCurrentContext();
        auto world = v8::Object::New(isolate);

        auto put = [&](const char *name, v8::FunctionCallback cb) {
            world->Set(ctx, v8pp::to_v8(isolate, name), v8::FunctionTemplate::New(isolate, cb)->GetFunction(ctx).ToLocalChecked()).Check();
        };

        put("setTime", &World::JS_SetTime);
        put("getHour", &JS_Get<&World::GetHour>);
        put("setHour", &JS_Set<&World::SetHour, std::uint8_t>);
        put("getMinute", &JS_Get<&World::GetMinute>);
        put("setMinute", &JS_Set<&World::SetMinute, std::uint8_t>);
        put("getMinuteDuration", &JS_Get<&World::GetMinuteDuration>);
        put("setMinuteDuration", &JS_Set<&World::SetMinuteDuration, std::uint32_t>);
        put("getGameSpeed", &JS_Get<&World::GetGameSpeed>);
        put("setGameSpeed", &JS_Set<&World::SetGameSpeed, float>);
        put("getWeather", &JS_Get<&World::GetWeather>);
        put("setWeather", &JS_Set<&World::SetWeather, std::uint8_t>);
        put("getGravity", &JS_Get<&World::GetGravity>);
        put("setGravity", &JS_Set<&World::SetGravity, float>);
        put("getFarClip", &JS_Get<&World::GetFarClip>);
        put("setFarClip", &JS_Set<&World::SetFarClip, float>);
        put("getFogDistance", &JS_Get<&World::GetFogDistance>);
        put("setFogDistance", &JS_Set<&World::SetFogDistance, float>);
        put("getRainLevel", &JS_Get<&World::GetRainLevel>);
        put("setRainLevel", &JS_Set<&World::SetRainLevel, float>);
        put("getWaveHeight", &JS_Get<&World::GetWaveHeight>);
        put("setWaveHeight", &JS_Set<&World::SetWaveHeight, float>);
        put("setSkyGradient", &World::JS_SetSkyGradient);
        put("getSkyGradient", &World::JS_GetSkyGradient);
        put("getMoonSize", &JS_Get<&World::GetMoonSize>);
        put("setMoonSize", &JS_Set<&World::SetMoonSize, std::uint8_t>);
        put("setSunColor", &World::JS_SetSunColor);
        put("getSunColor", &World::JS_GetSunColor);
        put("getSunSize", &JS_Get<&World::GetSunSize>);
        put("setSunSize", &JS_Set<&World::SetSunSize, float>);
        put("setWaterColor", &World::JS_SetWaterColor);
        put("getWaterColor", &World::JS_GetWaterColor);
        put("setHeatHaze", &World::JS_SetHeatHaze);
        put("getHeatHaze", &World::JS_GetHeatHaze);
        put("setCloudsEnabled", &World::JS_SetCloudsEnabled);
        put("getCloudsEnabled", &JS_Get<&World::GetCloudsEnabled>);
        put("getJetpackMaxHeight", &JS_Get<&World::GetJetpackMaxHeight>);
        put("setJetpackMaxHeight", &JS_Set<&World::SetJetpackMaxHeight, float>);
        put("setWindVelocity", &World::JS_SetWindVelocity);
        put("getWindVelocity", &World::JS_GetWindVelocity);

        global->Set(ctx, v8pp::to_v8(isolate, "World"), world).Check();

        auto &meta = Framework::Scripting::GetScriptingCatalog(isolate).global_object(
            "World", "Server-authoritative clock, weather, gravity, and world visuals.");

        const auto getNum  = [](const char *desc) {
            return v8pp::metadata::docs("number", {}, desc, "Current value.");
        };
        const auto setNum  = [](const char *param, const char *desc) {
            return v8pp::metadata::docs("void", {v8pp::metadata::param(param, "number", false, desc)}, "Updates the shared world value.");
        };
        const auto getBool = [](const char *desc) {
            return v8pp::metadata::docs("boolean", {}, desc, "Current value.");
        };

        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>(
            "setTime", v8pp::metadata::docs("void",
                               {v8pp::metadata::param("hour", "number", false, "Hour 0..23."),
                                   v8pp::metadata::param("minute", "number", false, "Minute 0..59.")},
                               "Sets the shared clock.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("getHour", getNum("Reads the shared clock hour.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("setHour", setNum("hour", "Hour 0..23.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("getMinute", getNum("Reads the shared clock minute.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("setMinute", setNum("minute", "Minute 0..59.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("getMinuteDuration", getNum("Real milliseconds per in-game minute.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("setMinuteDuration", setNum("ms", "Real milliseconds per in-game minute.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("getGameSpeed", getNum("Simulation timescale (0..10).")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("setGameSpeed", setNum("speed", "Simulation timescale 0..10.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("getWeather", getNum("ForceWeatherNow weather id.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("setWeather", setNum("weather", "ForceWeatherNow weather id.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("getGravity", getNum("Global gravity (default 0.008).")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("setGravity", setNum("gravity", "Global gravity 0..1.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("getFarClip", getNum("Far clip distance.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("setFarClip", setNum("distance", "Far clip distance.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("getFogDistance", getNum("Fog start distance.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("setFogDistance", setNum("distance", "Fog start distance.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("getRainLevel", getNum("Rain level 0..1.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("setRainLevel", setNum("level", "Rain level 0..1.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("getWaveHeight", getNum("Water wave height.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("setWaveHeight", setNum("height", "Water wave height.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>(
            "setSkyGradient",
            v8pp::metadata::docs("void",
                {v8pp::metadata::param("topR", "number", false, "Sky top red 0..255."), v8pp::metadata::param("topG", "number", false, "Sky top green 0..255."),
                    v8pp::metadata::param("topB", "number", false, "Sky top blue 0..255."), v8pp::metadata::param("botR", "number", false, "Sky bottom red 0..255."),
                    v8pp::metadata::param("botG", "number", false, "Sky bottom green 0..255."), v8pp::metadata::param("botB", "number", false, "Sky bottom blue 0..255.")},
                "Sets the sky top and bottom RGB gradient.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>(
            "getSkyGradient", v8pp::metadata::docs("number[]", {}, "Reads the sky gradient.", "[topR, topG, topB, botR, botG, botB].")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("getMoonSize", getNum("Moon sprite size (default 3).")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("setMoonSize", setNum("size", "Moon sprite size.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>(
            "setSunColor",
            v8pp::metadata::docs("void",
                {v8pp::metadata::param("coreR", "number", false, "Sun core red."), v8pp::metadata::param("coreG", "number", false, "Sun core green."),
                    v8pp::metadata::param("coreB", "number", false, "Sun core blue."), v8pp::metadata::param("coronaR", "number", false, "Corona red."),
                    v8pp::metadata::param("coronaG", "number", false, "Corona green."), v8pp::metadata::param("coronaB", "number", false, "Corona blue.")},
                "Sets sun core and corona RGB.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>(
            "getSunColor", v8pp::metadata::docs("number[]", {}, "Reads sun colours.", "[coreR, coreG, coreB, coronaR, coronaG, coronaB].")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("getSunSize", getNum("Sun size (script float; GTA stores value * 10).")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("setSunSize", setNum("size", "Sun size.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>(
            "setWaterColor",
            v8pp::metadata::docs("void",
                {v8pp::metadata::param("r", "number", false, "Red 0..255."), v8pp::metadata::param("g", "number", false, "Green 0..255."),
                    v8pp::metadata::param("b", "number", false, "Blue 0..255."), v8pp::metadata::param("a", "number", true, "Alpha 0..255 (default 255).")},
                "Sets surface and underwater water colour.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>(
            "getWaterColor", v8pp::metadata::docs("number[]", {}, "Reads water colour.", "[r, g, b, a].")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>(
            "setHeatHaze",
            v8pp::metadata::docs("void",
                {v8pp::metadata::param("intensity", "number", false, "Heat haze intensity."),
                    v8pp::metadata::param("randomShift", "number", true, "Optional FX param."), v8pp::metadata::param("speedMin", "number", true, "Optional FX param."),
                    v8pp::metadata::param("speedMax", "number", true, "Optional FX param."), v8pp::metadata::param("scanX", "number", true, "Optional FX param."),
                    v8pp::metadata::param("scanY", "number", true, "Optional FX param."), v8pp::metadata::param("renderX", "number", true, "Optional FX param."),
                    v8pp::metadata::param("renderY", "number", true, "Optional FX param."), v8pp::metadata::param("bInsideBuilding", "boolean", true, "Optional inside-building flag.")},
                "Sets heat haze post-FX parameters.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>(
            "getHeatHaze", v8pp::metadata::docs("number[]", {}, "Reads heat haze parameters.", "[intensity, randomShift, speedMin, speedMax, scanX, scanY, renderX, renderY, inside].")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("getCloudsEnabled", getBool("Whether volumetric/bottom/skyline clouds are enabled.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>(
            "setCloudsEnabled", v8pp::metadata::docs("void", {v8pp::metadata::param("enabled", "boolean", false, "Enable cloud rendering.")}, "Toggles cloud rendering patches.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("getJetpackMaxHeight", getNum("Jetpack ceiling height (default 100).")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>("setJetpackMaxHeight", setNum("height", "Jetpack ceiling height.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>(
            "setWindVelocity",
            v8pp::metadata::docs("void",
                {v8pp::metadata::param("x", "number", false, "Wind X."), v8pp::metadata::param("y", "number", false, "Wind Y."),
                    v8pp::metadata::param("z", "number", false, "Wind Z.")},
                "Sets weather wind velocity.")));
        meta.record(v8pp::metadata::function_of<v8::FunctionCallback>(
            "getWindVelocity", v8pp::metadata::docs("number[]", {}, "Reads wind velocity.", "[x, y, z].")));
    }
} // namespace VantaMP::Scripting
