#include "world_manager.h"

#include <core_modules.h>
#include <networking/network_peer.h>

#include <algorithm>
#include <chrono>

namespace VantaMP::Game {
    namespace {
        std::uint64_t NowMs() {
            using clock = std::chrono::steady_clock;
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
        }
    } // namespace

    void WorldManager::GetTime(std::uint8_t &hour, std::uint8_t &minute) const {
        if (!_haveMidnight || _minuteDuration == 0) {
            hour   = 12;
            minute = 0;
            return;
        }

        const auto elapsed       = NowMs() - _midnightMs;
        const auto totalMinutes  = elapsed / _minuteDuration;
        hour                     = static_cast<std::uint8_t>((totalMinutes / 60) % 24);
        minute                   = static_cast<std::uint8_t>(totalMinutes % 60);
    }

    void WorldManager::SetTime(std::uint8_t hour, std::uint8_t minute) {
        hour   = static_cast<std::uint8_t>(hour % 24);
        minute = static_cast<std::uint8_t>(minute % 60);

        const auto minsFromMidnight = static_cast<std::uint64_t>(hour) * 60u + minute;
        _midnightMs                 = NowMs() - minsFromMidnight * _minuteDuration;
        _haveMidnight               = true;

        Broadcast(Shared::kWorldEnvTime);
    }

    void WorldManager::SetMinuteDuration(std::uint32_t ms) {
        if (ms == 0) {
            ms = 1;
        }

        std::uint8_t hour = 12, minute = 0;
        GetTime(hour, minute);

        _minuteDuration = ms;
        const auto minsFromMidnight = static_cast<std::uint64_t>(hour) * 60u + minute;
        _midnightMs                 = NowMs() - minsFromMidnight * _minuteDuration;
        _haveMidnight               = true;

        Broadcast(Shared::kWorldEnvMinuteDuration | Shared::kWorldEnvTime);
    }

    void WorldManager::SetGameSpeed(float speed) {
        _gameSpeed = std::clamp(speed, 0.0f, 10.0f);
        Broadcast(Shared::kWorldEnvGameSpeed);
    }

    void WorldManager::SetWeather(std::uint8_t weather) {
        _weather = weather;
        Broadcast(Shared::kWorldEnvWeather);
    }

    Shared::RPC::WorldEnv WorldManager::MakePacket(std::uint8_t flags) const {
        Shared::RPC::WorldEnv packet;
        packet.flags = flags;
        GetTime(packet.hour, packet.minute);
        packet.minuteDuration = _minuteDuration;
        packet.gameSpeed      = _gameSpeed;
        packet.weather        = _weather;
        return packet;
    }

    void WorldManager::SendTo(MafiaNet::PeerGuid guid) const {
        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net || static_cast<std::uint64_t>(guid) == 0) {
            return;
        }

        auto packet = MakePacket(Shared::kWorldEnvAllFlags);
        net->SendRPC(packet, MafiaNet::ToGuid(guid));
    }

    void WorldManager::Broadcast(std::uint8_t flags) const {
        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net || flags == 0) {
            return;
        }

        auto packet = MakePacket(flags);
        net->BroadcastRPC(packet, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }
} // namespace VantaMP::Game
