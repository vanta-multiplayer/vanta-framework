#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared {
    inline constexpr std::uint8_t kWorldEnvTime           = 1;
    inline constexpr std::uint8_t kWorldEnvMinuteDuration = 2;
    inline constexpr std::uint8_t kWorldEnvGameSpeed      = 4;
    inline constexpr std::uint8_t kWorldEnvWeather        = 8;

    inline constexpr std::uint8_t  kWorldEnvAllFlags =
        kWorldEnvTime | kWorldEnvMinuteDuration | kWorldEnvGameSpeed | kWorldEnvWeather;

    inline constexpr std::uint32_t kDefaultMinuteDuration = 1000;
    inline constexpr float         kDefaultGameSpeed      = 1.0f;
} // namespace VantaMP::Shared

namespace VantaMP::Shared::RPC {
    // Server-authoritative world clock / speed / weather. Broadcast to
    // everyone, and resent on join so late clients match. Not a player
    // field — riding PlayerScript would leave remotes on the wrong day.
    struct WorldEnv {
        static constexpr const char *kIdentifier = "VantaMP::WorldEnv";

        std::uint8_t  flags          = 0;
        std::uint8_t  hour           = 12;
        std::uint8_t  minute         = 0;
        std::uint32_t minuteDuration = Shared::kDefaultMinuteDuration;
        float         gameSpeed      = Shared::kDefaultGameSpeed;
        std::uint8_t  weather        = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, flags);
            bs->Serialize(write, hour);
            bs->Serialize(write, minute);
            bs->Serialize(write, minuteDuration);
            bs->Serialize(write, gameSpeed);
            bs->Serialize(write, weather);
        }
    };
} // namespace VantaMP::Shared::RPC
