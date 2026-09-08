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
    }

    void PulseWorldEnv() {
        // MTA rewrites ms_fTimeScale every pulse. A cheat that pokes the
        // float is undone before the next physics step.
        SDK::EnforceGameSpeed();
    }
} // namespace VantaMP::Game
