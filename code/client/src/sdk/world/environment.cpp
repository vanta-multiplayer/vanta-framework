#include "environment.h"

#include "sdk/patterns.h"

#include <algorithm>

namespace VantaMP::SDK {
    namespace {
        float gAuthoritativeGameSpeed = 1.0f;
        float gAuthoritativeGravity   = 0.008f;
        float gFarClip                = 0.0f;
        float gFogDistance            = 0.0f;
        float gRainLevel              = 0.0f;
        float gWaveHeight             = 0.0f;
        bool  gHaveFarClip            = false;
        bool  gHaveFog                = false;
        bool  gHaveRain               = false;
        bool  gHaveWave               = false;

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

        float *Gravity() {
            return reinterpret_cast<float *>(gPatterns.CGame__ms_fGravity);
        }

        float *FarClip() {
            return reinterpret_cast<float *>(gPatterns.CRenderer__ms_fFarClipPlane);
        }

        float *FogDistance() {
            return reinterpret_cast<float *>(gPatterns.CRenderer__ms_fFogStart);
        }

        float *Rain() {
            return reinterpret_cast<float *>(gPatterns.CWeather__Rain);
        }

        float *WaveLevel() {
            return reinterpret_cast<float *>(gPatterns.CWater__ms_fWaveHeight);
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

    void SetGravity(float gravity) {
        gAuthoritativeGravity = std::clamp(gravity, 0.0f, 1.0f);
        EnforceGravity();
    }

    void EnforceGravity() {
        if (auto *gravity = Gravity()) {
            *gravity = gAuthoritativeGravity;
        }
    }

    void SetFarClip(float distance) {
        gFarClip     = distance;
        gHaveFarClip = true;
        EnforceWorldVisuals();
    }

    void SetFogDistance(float distance) {
        gFogDistance = distance;
        gHaveFog     = true;
        EnforceWorldVisuals();
    }

    void SetRainLevel(float level) {
        gRainLevel = std::clamp(level, 0.0f, 1.0f);
        gHaveRain  = true;
        EnforceWorldVisuals();
    }

    void SetWaveHeight(float height) {
        gWaveHeight = std::max(0.0f, height);
        gHaveWave   = true;
        EnforceWorldVisuals();
    }

    void EnforceWorldVisuals() {
        if (gHaveFarClip) {
            if (auto *farClip = FarClip()) {
                *farClip = gFarClip;
            }
        }

        if (gHaveFog) {
            if (auto *fog = FogDistance()) {
                *fog = gFogDistance;
            }
        }

        if (gHaveRain) {
            if (auto *rain = Rain()) {
                *rain = gRainLevel;
            }
        }

        if (gHaveWave) {
            if (auto *wave = WaveLevel()) {
                *wave = gWaveHeight;
            }
        }
    }
} // namespace VantaMP::SDK
