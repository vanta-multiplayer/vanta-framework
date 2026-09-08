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

    void World::JS_SetTime(const v8::FunctionCallbackInfo<v8::Value> &info) {
        if (info.Length() < 2) {
            return;
        }

        auto *isolate = info.GetIsolate();
        SetTime(v8pp::from_v8<std::uint8_t>(isolate, info[0]), v8pp::from_v8<std::uint8_t>(isolate, info[1]));
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

        global->Set(ctx, v8pp::to_v8(isolate, "World"), world).Check();
    }
} // namespace VantaMP::Scripting
