#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared {
    inline constexpr std::uint8_t kPlayerScriptHealth  = 1;
    inline constexpr std::uint8_t kPlayerScriptArmour  = 2;
    inline constexpr std::uint8_t kPlayerScriptModel   = 4;
    inline constexpr std::uint8_t kPlayerScriptMoney   = 8;
    inline constexpr std::uint8_t kPlayerScriptWanted  = 16;
    inline constexpr std::uint8_t kPlayerScriptFrozen  = 32;

    // Second flag byte. The first is full. These bits go in flags2.
    inline constexpr std::uint8_t kPlayerScript2Collisions    = 1;
    inline constexpr std::uint8_t kPlayerScript2FightingStyle = 2;
    inline constexpr std::uint8_t kPlayerScript2Oxygen        = 4;
    inline constexpr std::uint8_t kPlayerScript2WalkingStyle  = 8;
    inline constexpr std::uint8_t kPlayerScript2Alpha         = 16;
    inline constexpr std::uint8_t kPlayerScript2OnFire        = 32;
} // namespace VantaMP::Shared

namespace VantaMP::Shared::RPC {
    // Script writing fields the owner reports (or echoes). Same reason as
    // WorldSpace: the next snapshot would send the old GTA values unless
    // the owning client applies them first.
    struct PlayerScript {
        static constexpr const char *kIdentifier = "VantaMP::PlayerScript";

        std::uint8_t  flags       = 0;
        float         health      = 100.0f;
        float         armour      = 0.0f;
        std::uint32_t modelId     = 0;
        std::int32_t  money       = 0;
        std::uint8_t  wantedLevel = 0;
        bool          frozen      = false;

        // Appended. flags2 uses kPlayerScript2Collisions and friends.
        std::uint8_t flags2         = 0;
        bool         collisions     = true;
        std::uint8_t  fightingStyle = 0;
        float         oxygen        = 1000.0f;
        std::uint16_t walkingStyle  = 0;
        std::uint8_t  alpha         = 255;
        bool          onFire        = false;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, flags);
            bs->Serialize(write, health);
            bs->Serialize(write, armour);
            bs->Serialize(write, modelId);
            bs->Serialize(write, money);
            bs->Serialize(write, wantedLevel);
            bs->Serialize(write, frozen);
            bs->Serialize(write, flags2);
            bs->Serialize(write, collisions);
            bs->Serialize(write, fightingStyle);
            bs->Serialize(write, oxygen);
            bs->Serialize(write, walkingStyle);
            bs->Serialize(write, alpha);
            bs->Serialize(write, onFire);
        }
    };
} // namespace VantaMP::Shared::RPC
