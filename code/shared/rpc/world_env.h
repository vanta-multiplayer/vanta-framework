#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared {
    inline constexpr std::uint16_t kWorldEnvTime           = 1;
    inline constexpr std::uint16_t kWorldEnvMinuteDuration = 2;
    inline constexpr std::uint16_t kWorldEnvGameSpeed      = 4;
    inline constexpr std::uint16_t kWorldEnvWeather        = 8;
    inline constexpr std::uint16_t kWorldEnvGravity        = 16;
    inline constexpr std::uint16_t kWorldEnvFarClip        = 32;
    inline constexpr std::uint16_t kWorldEnvFog            = 64;
    inline constexpr std::uint16_t kWorldEnvRain           = 128;
    inline constexpr std::uint16_t kWorldEnvWave           = 256;

    // Always on join. Optional visual overrides are OR'd when script has set them.
    inline constexpr std::uint16_t kWorldEnvCoreFlags =
        kWorldEnvTime | kWorldEnvMinuteDuration | kWorldEnvGameSpeed | kWorldEnvWeather | kWorldEnvGravity;

    inline constexpr std::uint32_t kDefaultMinuteDuration = 1000;
    inline constexpr float         kDefaultGameSpeed      = 1.0f;
    inline constexpr float         kDefaultGravity        = 0.008f;
} // namespace VantaMP::Shared

namespace VantaMP::Shared::RPC {
    // Server-authoritative world clock / speed / weather / gravity / visuals.
    // Broadcast to everyone, and resent on join so late clients match. Not a
    // player field — riding PlayerScript would leave remotes on the wrong day.
    struct WorldEnv {
        static constexpr const char *kIdentifier = "VantaMP::WorldEnv";

        std::uint16_t flags          = 0;
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
        }
    };
} // namespace VantaMP::Shared::RPC
