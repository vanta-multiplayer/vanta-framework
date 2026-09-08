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

        const auto elapsed      = NowMs() - _midnightMs;
        const auto totalMinutes = elapsed / _minuteDuration;
        hour                    = static_cast<std::uint8_t>((totalMinutes / 60) % 24);
        minute                  = static_cast<std::uint8_t>(totalMinutes % 60);
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

        _minuteDuration             = ms;
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

    void WorldManager::SetGravity(float gravity) {
        _gravity = std::clamp(gravity, 0.0f, 1.0f);
        Broadcast(Shared::kWorldEnvGravity);
    }

    void WorldManager::SetFarClip(float distance) {
        _farClip    = distance;
        _farClipSet = true;
        Broadcast(Shared::kWorldEnvFarClip);
    }

    void WorldManager::SetFogDistance(float distance) {
        _fogDistance = distance;
        _fogSet      = true;
        Broadcast(Shared::kWorldEnvFog);
    }

    void WorldManager::SetRainLevel(float level) {
        _rainLevel = std::clamp(level, 0.0f, 1.0f);
        _rainSet   = true;
        Broadcast(Shared::kWorldEnvRain);
    }

    void WorldManager::SetWaveHeight(float height) {
        _waveHeight = std::max(0.0f, height);
        _waveSet    = true;
        Broadcast(Shared::kWorldEnvWave);
    }

    void WorldManager::GetSkyGradient(std::uint8_t &topR, std::uint8_t &topG, std::uint8_t &topB, std::uint8_t &botR, std::uint8_t &botG,
                                      std::uint8_t &botB) const {
        topR = _skyTopR;
        topG = _skyTopG;
        topB = _skyTopB;
        botR = _skyBotR;
        botG = _skyBotG;
        botB = _skyBotB;
    }

    void WorldManager::SetSkyGradient(std::uint8_t topR, std::uint8_t topG, std::uint8_t topB, std::uint8_t botR, std::uint8_t botG, std::uint8_t botB) {
        _skyTopR = topR;
        _skyTopG = topG;
        _skyTopB = topB;
        _skyBotR = botR;
        _skyBotG = botG;
        _skyBotB = botB;
        _skySet  = true;
        Broadcast(Shared::kWorldEnvSky);
    }

    void WorldManager::SetMoonSize(std::uint8_t size) {
        _moonSize = size;
        _moonSet  = true;
        Broadcast(Shared::kWorldEnvMoon);
    }

    void WorldManager::GetSunColor(std::uint8_t &coreR, std::uint8_t &coreG, std::uint8_t &coreB, std::uint8_t &coronaR, std::uint8_t &coronaG,
                                   std::uint8_t &coronaB) const {
        coreR   = _sunCoreR;
        coreG   = _sunCoreG;
        coreB   = _sunCoreB;
        coronaR = _sunCoronaR;
        coronaG = _sunCoronaG;
        coronaB = _sunCoronaB;
    }

    void WorldManager::SetSunColor(std::uint8_t coreR, std::uint8_t coreG, std::uint8_t coreB, std::uint8_t coronaR, std::uint8_t coronaG,
                                   std::uint8_t coronaB) {
        _sunCoreR    = coreR;
        _sunCoreG    = coreG;
        _sunCoreB    = coreB;
        _sunCoronaR  = coronaR;
        _sunCoronaG  = coronaG;
        _sunCoronaB  = coronaB;
        _sunColorSet = true;
        Broadcast(Shared::kWorldEnvSunColor);
    }

    void WorldManager::SetSunSize(float size) {
        _sunSize    = std::max(0.0f, size);
        _sunSizeSet = true;
        Broadcast(Shared::kWorldEnvSunSize);
    }

    std::uint16_t WorldManager::JoinFlags() const {
        auto flags = Shared::kWorldEnvCoreFlags;

        if (_farClipSet) {
            flags |= Shared::kWorldEnvFarClip;
        }

        if (_fogSet) {
            flags |= Shared::kWorldEnvFog;
        }

        if (_rainSet) {
            flags |= Shared::kWorldEnvRain;
        }

        if (_waveSet) {
            flags |= Shared::kWorldEnvWave;
        }

        if (_skySet) {
            flags |= Shared::kWorldEnvSky;
        }

        if (_moonSet) {
            flags |= Shared::kWorldEnvMoon;
        }

        if (_sunColorSet) {
            flags |= Shared::kWorldEnvSunColor;
        }

        if (_sunSizeSet) {
            flags |= Shared::kWorldEnvSunSize;
        }

        return flags;
    }

    Shared::RPC::WorldEnv WorldManager::MakePacket(std::uint16_t flags) const {
        Shared::RPC::WorldEnv packet;
        packet.flags = flags;
        GetTime(packet.hour, packet.minute);
        packet.minuteDuration = _minuteDuration;
        packet.gameSpeed      = _gameSpeed;
        packet.weather        = _weather;
        packet.gravity        = _gravity;
        packet.farClip        = _farClip;
        packet.fogDistance    = _fogDistance;
        packet.rainLevel      = _rainLevel;
        packet.waveHeight     = _waveHeight;
        packet.skyTopR        = _skyTopR;
        packet.skyTopG        = _skyTopG;
        packet.skyTopB        = _skyTopB;
        packet.skyBotR        = _skyBotR;
        packet.skyBotG        = _skyBotG;
        packet.skyBotB        = _skyBotB;
        packet.moonSize       = _moonSize;
        packet.sunCoreR       = _sunCoreR;
        packet.sunCoreG       = _sunCoreG;
        packet.sunCoreB       = _sunCoreB;
        packet.sunCoronaR     = _sunCoronaR;
        packet.sunCoronaG     = _sunCoronaG;
        packet.sunCoronaB     = _sunCoronaB;
        packet.sunSize        = _sunSize;
        return packet;
    }

    void WorldManager::SendTo(MafiaNet::PeerGuid guid) const {
        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net || static_cast<std::uint64_t>(guid) == 0) {
            return;
        }

        auto packet = MakePacket(JoinFlags());
        net->SendRPC(packet, MafiaNet::ToGuid(guid));
    }

    void WorldManager::Broadcast(std::uint16_t flags) const {
        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net || flags == 0) {
            return;
        }

        auto packet = MakePacket(flags);
        net->BroadcastRPC(packet, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }
} // namespace VantaMP::Game
