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

        // Full snapshot to one peer (join). Partial broadcast for script sets.
        void SendTo(MafiaNet::PeerGuid guid) const;
        void Broadcast(std::uint8_t flags) const;

      private:
        Shared::RPC::WorldEnv MakePacket(std::uint8_t flags) const;

        std::uint64_t _midnightMs     = 0;
        std::uint32_t _minuteDuration = Shared::kDefaultMinuteDuration;
        float         _gameSpeed      = Shared::kDefaultGameSpeed;
        std::uint8_t  _weather        = 0;
        bool          _haveMidnight   = false;
    };
} // namespace VantaMP::Game
