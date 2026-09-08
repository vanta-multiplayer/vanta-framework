#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared {
    inline constexpr std::uint32_t kWorldEnvTime           = 1;
    inline constexpr std::uint32_t kWorldEnvMinuteDuration = 2;
    inline constexpr std::uint32_t kWorldEnvGameSpeed      = 4;
    inline constexpr std::uint32_t kWorldEnvWeather        = 8;
    inline constexpr std::uint32_t kWorldEnvGravity        = 16;
    inline constexpr std::uint32_t kWorldEnvFarClip        = 32;
    inline constexpr std::uint32_t kWorldEnvFog            = 64;
    inline constexpr std::uint32_t kWorldEnvRain           = 128;
    inline constexpr std::uint32_t kWorldEnvWave           = 256;
    inline constexpr std::uint32_t kWorldEnvSky            = 512;
    inline constexpr std::uint32_t kWorldEnvMoon           = 1024;
    inline constexpr std::uint32_t kWorldEnvSunColor       = 2048;
    inline constexpr std::uint32_t kWorldEnvSunSize        = 4096;
    inline constexpr std::uint32_t kWorldEnvWaterColor     = 8192;
    inline constexpr std::uint32_t kWorldEnvHeatHaze       = 16384;
    inline constexpr std::uint32_t kWorldEnvClouds         = 32768;
    inline constexpr std::uint32_t kWorldEnvJetpackHeight  = 65536;
    inline constexpr std::uint32_t kWorldEnvWind           = 131072;

    // Always on join. Optional visual overrides are OR'd when script has set them.
    inline constexpr std::uint32_t kWorldEnvCoreFlags =
        kWorldEnvTime | kWorldEnvMinuteDuration | kWorldEnvGameSpeed | kWorldEnvWeather | kWorldEnvGravity;

    inline constexpr std::uint32_t kDefaultMinuteDuration = 1000;
    inline constexpr float         kDefaultGameSpeed      = 1.0f;
    inline constexpr float         kDefaultGravity        = 0.008f;
    inline constexpr std::uint8_t  kDefaultMoonSize       = 3;
    inline constexpr float         kDefaultJetpackHeight  = 100.0f;
} // namespace VantaMP::Shared

namespace VantaMP::Shared::RPC {
    // Server-authoritative world clock / speed / weather / gravity / visuals.
    // Broadcast to everyone, and resent on join so late clients match. Not a
    // player field — riding PlayerScript would leave remotes on the wrong day.
    struct WorldEnv {
        static constexpr const char *kIdentifier = "VantaMP::WorldEnv";

        std::uint32_t flags          = 0;
        std::uint8_t  hour           = 12;
        std::uint8_t  minute         = 0;
        std::uint32_t minuteDuration = Shared::kDefaultMinuteDuration;
        float         gameSpeed      = Shared::kDefaultGameSpeed;
        std::uint8_t  weather        = 0;
        float         gravity        = Shared::kDefaultGravity;
        float         farClip        = 0.0f;
        float         fogDistance    = 0.0f;
        float         rainLevel      = 0.0f;
        float         waveHeight     = 0.0f;
        std::uint8_t  skyTopR = 0, skyTopG = 0, skyTopB = 0;
        std::uint8_t  skyBotR = 0, skyBotG = 0, skyBotB = 0;
        std::uint8_t  moonSize       = Shared::kDefaultMoonSize;
        std::uint8_t  sunCoreR = 0, sunCoreG = 0, sunCoreB = 0;
        std::uint8_t  sunCoronaR = 0, sunCoronaG = 0, sunCoronaB = 0;
        float         sunSize        = 1.0f;
        float         waterR = 0, waterG = 0, waterB = 0, waterA = 255;
        std::uint8_t  hazeIntensity  = 0;
        std::uint8_t  hazeRandomShift = 0;
        std::uint16_t hazeSpeedMin   = 12;
        std::uint16_t hazeSpeedMax   = 18;
        std::int16_t  hazeScanX      = 75;
        std::int16_t  hazeScanY      = 80;
        std::uint16_t hazeRenderX    = 80;
        std::uint16_t hazeRenderY    = 85;
        bool          hazeInsideBuilding = false;
        bool          cloudsEnabled  = true;
        float         jetpackMaxHeight = Shared::kDefaultJetpackHeight;
        float         windX = 0, windY = 0, windZ = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, flags);
            bs->Serialize(write, hour);
            bs->Serialize(write, minute);
            bs->Serialize(write, minuteDuration);
            bs->Serialize(write, gameSpeed);
            bs->Serialize(write, weather);
            bs->Serialize(write, gravity);
            bs->Serialize(write, farClip);
            bs->Serialize(write, fogDistance);
            bs->Serialize(write, rainLevel);
            bs->Serialize(write, waveHeight);
            bs->Serialize(write, skyTopR);
            bs->Serialize(write, skyTopG);
            bs->Serialize(write, skyTopB);
            bs->Serialize(write, skyBotR);
            bs->Serialize(write, skyBotG);
            bs->Serialize(write, skyBotB);
            bs->Serialize(write, moonSize);
            bs->Serialize(write, sunCoreR);
            bs->Serialize(write, sunCoreG);
            bs->Serialize(write, sunCoreB);
            bs->Serialize(write, sunCoronaR);
            bs->Serialize(write, sunCoronaG);
            bs->Serialize(write, sunCoronaB);
            bs->Serialize(write, sunSize);
            bs->Serialize(write, waterR);
            bs->Serialize(write, waterG);
            bs->Serialize(write, waterB);
            bs->Serialize(write, waterA);
            bs->Serialize(write, hazeIntensity);
            bs->Serialize(write, hazeRandomShift);
            bs->Serialize(write, hazeSpeedMin);
            bs->Serialize(write, hazeSpeedMax);
            bs->Serialize(write, hazeScanX);
            bs->Serialize(write, hazeScanY);
            bs->Serialize(write, hazeRenderX);
            bs->Serialize(write, hazeRenderY);
            bs->Serialize(write, hazeInsideBuilding);
            bs->Serialize(write, cloudsEnabled);
            bs->Serialize(write, jetpackMaxHeight);
            bs->Serialize(write, windX);
            bs->Serialize(write, windY);
            bs->Serialize(write, windZ);
        }
    };
} // namespace VantaMP::Shared::RPC
