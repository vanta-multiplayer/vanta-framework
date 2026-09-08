#pragma once

#include <cstdint>
#include <v8.h>

namespace VantaMP::Game {
    class WorldManager;
}

namespace VantaMP::Scripting {
    // Global World helpers — clock, speed, weather, gravity, and visuals.
    // Not an Entity; the server owns one environment for everyone.
    class World final {
      public:
        static void Bind(Game::WorldManager &world);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static std::uint8_t GetHour();
        static void SetHour(std::uint8_t hour);

        static std::uint8_t GetMinute();
        static void SetMinute(std::uint8_t minute);

        static void SetTime(std::uint8_t hour, std::uint8_t minute);

        static std::uint32_t GetMinuteDuration();
        static void SetMinuteDuration(std::uint32_t ms);

        static float GetGameSpeed();
        static void SetGameSpeed(float speed);

        static std::uint8_t GetWeather();
        static void SetWeather(std::uint8_t weather);

        static float GetGravity();
        static void SetGravity(float gravity);

        static float GetFarClip();
        static void SetFarClip(float distance);

        static float GetFogDistance();
        static void SetFogDistance(float distance);

        static float GetRainLevel();
        static void SetRainLevel(float level);

        static float GetWaveHeight();
        static void SetWaveHeight(float height);

        static std::uint8_t GetMoonSize();
        static void SetMoonSize(std::uint8_t size);

        static float GetSunSize();
        static void SetSunSize(float size);

      private:
        static void JS_SetTime(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetSkyGradient(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetSkyGradient(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetSunColor(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetSunColor(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetWaterColor(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetWaterColor(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetHeatHaze(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetHeatHaze(const v8::FunctionCallbackInfo<v8::Value> &info);

        inline static Game::WorldManager *gWorld = nullptr;
    };
} // namespace VantaMP::Scripting
