#pragma once

#include "shared/rpc/world_env.h"

#include <mafianet/types.h>

#include <cstdint>

namespace VantaMP::Game {
    // Server clock and world switches. Wall-tick based like MTA's CClock —
    // game speed does not advance the server clock (avoids day/night drift
    // between getTime and what GTA shows under setGameSpeed).
    class WorldManager {
      public:
        void GetTime(std::uint8_t &hour, std::uint8_t &minute) const;
        void SetTime(std::uint8_t hour, std::uint8_t minute);

        [[nodiscard]] std::uint32_t GetMinuteDuration() const {
            return _minuteDuration;
        }
        void SetMinuteDuration(std::uint32_t ms);

        [[nodiscard]] float GetGameSpeed() const {
            return _gameSpeed;
        }
        void SetGameSpeed(float speed);

        [[nodiscard]] std::uint8_t GetWeather() const {
            return _weather;
        }
        void SetWeather(std::uint8_t weather);

        [[nodiscard]] float GetGravity() const {
            return _gravity;
        }
        void SetGravity(float gravity);

        [[nodiscard]] float GetFarClip() const {
            return _farClip;
        }
        void SetFarClip(float distance);

        [[nodiscard]] float GetFogDistance() const {
            return _fogDistance;
        }
        void SetFogDistance(float distance);

        [[nodiscard]] float GetRainLevel() const {
            return _rainLevel;
        }
        void SetRainLevel(float level);

        [[nodiscard]] float GetWaveHeight() const {
            return _waveHeight;
        }
        void SetWaveHeight(float height);

        void GetSkyGradient(std::uint8_t &topR, std::uint8_t &topG, std::uint8_t &topB, std::uint8_t &botR, std::uint8_t &botG,
                            std::uint8_t &botB) const;
        void SetSkyGradient(std::uint8_t topR, std::uint8_t topG, std::uint8_t topB, std::uint8_t botR, std::uint8_t botG, std::uint8_t botB);

        [[nodiscard]] std::uint8_t GetMoonSize() const {
            return _moonSize;
        }
        void SetMoonSize(std::uint8_t size);

        void GetSunColor(std::uint8_t &coreR, std::uint8_t &coreG, std::uint8_t &coreB, std::uint8_t &coronaR, std::uint8_t &coronaG,
                         std::uint8_t &coronaB) const;
        void SetSunColor(std::uint8_t coreR, std::uint8_t coreG, std::uint8_t coreB, std::uint8_t coronaR, std::uint8_t coronaG, std::uint8_t coronaB);

        [[nodiscard]] float GetSunSize() const {
            return _sunSize;
        }
        void SetSunSize(float size);

        // Full snapshot to one peer (join). Partial broadcast for script sets.
        void SendTo(MafiaNet::PeerGuid guid) const;
        void Broadcast(std::uint16_t flags) const;

      private:
        [[nodiscard]] std::uint16_t JoinFlags() const;
        Shared::RPC::WorldEnv MakePacket(std::uint16_t flags) const;

        std::uint64_t _midnightMs     = 0;
        std::uint32_t _minuteDuration = Shared::kDefaultMinuteDuration;
        float         _gameSpeed      = Shared::kDefaultGameSpeed;
        std::uint8_t  _weather        = 0;
        float         _gravity        = Shared::kDefaultGravity;
        float         _farClip        = 0.0f;
        float         _fogDistance    = 0.0f;
        float         _rainLevel      = 0.0f;
        float         _waveHeight     = 0.0f;
        std::uint8_t  _skyTopR = 0, _skyTopG = 0, _skyTopB = 0;
        std::uint8_t  _skyBotR = 0, _skyBotG = 0, _skyBotB = 0;
        std::uint8_t  _moonSize       = Shared::kDefaultMoonSize;
        std::uint8_t  _sunCoreR = 0, _sunCoreG = 0, _sunCoreB = 0;
        std::uint8_t  _sunCoronaR = 0, _sunCoronaG = 0, _sunCoronaB = 0;
        float         _sunSize        = 1.0f;
        bool          _haveMidnight   = false;
        bool          _farClipSet     = false;
        bool          _fogSet         = false;
        bool          _rainSet        = false;
        bool          _waveSet        = false;
        bool          _skySet         = false;
        bool          _moonSet        = false;
        bool          _sunColorSet    = false;
        bool          _sunSizeSet     = false;
    };
} // namespace VantaMP::Game
