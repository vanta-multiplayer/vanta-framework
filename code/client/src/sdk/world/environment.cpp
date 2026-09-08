#include "environment.h"

#include "sdk/patterns.h"

#include <algorithm>

namespace VantaMP::SDK {
    namespace {
        float gAuthoritativeGameSpeed = 1.0f;

        std::uint8_t *ClockHours() {
            return reinterpret_cast<std::uint8_t *>(gPatterns.CClock__ms_nGameClockHours);
        }

        std::uint8_t *ClockMinutes() {
            return reinterpret_cast<std::uint8_t *>(gPatterns.CClock__ms_nGameClockMinutes);
        }

        std::uint32_t *MinuteDuration() {
            return reinterpret_cast<std::uint32_t *>(gPatterns.CClock__ms_nMillisecondsPerGameMinute);
        }

        float *TimeScale() {
            return reinterpret_cast<float *>(gPatterns.CTimer__ms_fTimeScale);
        }
    } // namespace

    void SetGameClock(std::uint8_t hour, std::uint8_t minute) {
        if (auto *hours = ClockHours()) {
            *hours = static_cast<std::uint8_t>(hour % 24);
        }

        if (auto *minutes = ClockMinutes()) {
            *minutes = static_cast<std::uint8_t>(minute % 60);
        }
    }

    void SetMinuteDuration(std::uint32_t millisecondsPerGameMinute) {
        if (auto *duration = MinuteDuration()) {
            *duration = millisecondsPerGameMinute == 0 ? 1u : millisecondsPerGameMinute;
        }
    }

    void SetGameSpeed(float speed) {
        gAuthoritativeGameSpeed = std::clamp(speed, 0.0f, 10.0f);
        EnforceGameSpeed();
    }

    void EnforceGameSpeed() {
        if (auto *scale = TimeScale()) {
            *scale = gAuthoritativeGameSpeed;
        }
    }

    float GameSpeed() {
        return gAuthoritativeGameSpeed;
    }

    void SetWeather(std::uint8_t weatherId) {
        if (!gPatterns.CWeather__ForceWeatherNow) {
            return;
        }

        using ForceFn = void(__cdecl *)(short);

        reinterpret_cast<ForceFn>(gPatterns.CWeather__ForceWeatherNow)(static_cast<short>(weatherId));
    }
} // namespace VantaMP::SDK
