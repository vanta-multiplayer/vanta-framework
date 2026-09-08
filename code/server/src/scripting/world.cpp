#include "world.h"

#include "game/world_manager.h"
#include "shared/rpc/world_env.h"

#include <v8pp/convert.hpp>

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

        global->Set(ctx, v8pp::to_v8(isolate, "World"), world).Check();
    }
} // namespace VantaMP::Scripting
