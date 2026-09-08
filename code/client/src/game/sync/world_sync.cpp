#include "game/sync/world_sync.h"

#include "sdk/world/environment.h"

namespace VantaMP::Game {
    void ApplyWorldEnv(const Shared::RPC::WorldEnv &env) {
        if ((env.flags & Shared::kWorldEnvTime) != 0) {
            SDK::SetGameClock(env.hour, env.minute);
        }

        if ((env.flags & Shared::kWorldEnvMinuteDuration) != 0) {
            SDK::SetMinuteDuration(env.minuteDuration);
        }

        if ((env.flags & Shared::kWorldEnvGameSpeed) != 0) {
            SDK::SetGameSpeed(env.gameSpeed);
        }

        if ((env.flags & Shared::kWorldEnvWeather) != 0) {
            SDK::SetWeather(env.weather);
        }

        if ((env.flags & Shared::kWorldEnvGravity) != 0) {
            SDK::SetGravity(env.gravity);
        }

        if ((env.flags & Shared::kWorldEnvFarClip) != 0) {
            SDK::SetFarClip(env.farClip);
        }

        if ((env.flags & Shared::kWorldEnvFog) != 0) {
            SDK::SetFogDistance(env.fogDistance);
        }

        if ((env.flags & Shared::kWorldEnvRain) != 0) {
            SDK::SetRainLevel(env.rainLevel);
        }

        if ((env.flags & Shared::kWorldEnvWave) != 0) {
            SDK::SetWaveHeight(env.waveHeight);
        }

        if ((env.flags & Shared::kWorldEnvSky) != 0) {
            SDK::SetSkyGradient(env.skyTopR, env.skyTopG, env.skyTopB, env.skyBotR, env.skyBotG, env.skyBotB);
        }

        if ((env.flags & Shared::kWorldEnvMoon) != 0) {
            SDK::SetMoonSize(env.moonSize);
        }

        if ((env.flags & Shared::kWorldEnvSunColor) != 0) {
            SDK::SetSunColor(env.sunCoreR, env.sunCoreG, env.sunCoreB, env.sunCoronaR, env.sunCoronaG, env.sunCoronaB);
        }

        if ((env.flags & Shared::kWorldEnvSunSize) != 0) {
            SDK::SetSunSize(env.sunSize);
        }
    }

    void PulseWorldEnv() {
        // MTA rewrites timescale / gravity (and visual overrides) every pulse.
        // A cheat that pokes those floats is undone before the next physics step.
        SDK::EnforceGameSpeed();
        SDK::EnforceGravity();
        SDK::EnforceWorldVisuals();
    }
} // namespace VantaMP::Game
