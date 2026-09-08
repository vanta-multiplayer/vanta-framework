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
        float gSunSize                = 1.0f;
        std::uint8_t gSkyTopR = 0, gSkyTopG = 0, gSkyTopB = 0;
        std::uint8_t gSkyBotR = 0, gSkyBotG = 0, gSkyBotB = 0;
        std::uint8_t gMoonSize = 3;
        std::uint8_t gSunCoreR = 0, gSunCoreG = 0, gSunCoreB = 0;
        std::uint8_t gSunCoronaR = 0, gSunCoronaG = 0, gSunCoronaB = 0;
        bool  gHaveFarClip            = false;
        bool  gHaveFog                = false;
        bool  gHaveRain               = false;
        bool  gHaveWave               = false;
        bool  gHaveSky                = false;
        bool  gHaveMoon               = false;
        bool  gHaveSunColor           = false;
        bool  gHaveSunSize            = false;

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

        // Sky / sun colour channels sit on even offsets (WORD-aligned bytes).
        std::uint8_t *SkyBase() {
            return reinterpret_cast<std::uint8_t *>(gPatterns.CTimeCycle__m_nSkyTopRed);
        }

        std::uint8_t *SunColorBase() {
            return reinterpret_cast<std::uint8_t *>(gPatterns.CTimeCycle__m_nSunCoreRed);
        }

        std::uint8_t *MoonSize() {
            return reinterpret_cast<std::uint8_t *>(gPatterns.CCoronas__MoonSize);
        }

        float *SunSize() {
            return reinterpret_cast<float *>(gPatterns.CTimeCycle__m_fSunSize);
        }

        void WriteRgbStride2(std::uint8_t *base, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            if (!base) {
                return;
            }

            base[0] = r;
            base[2] = g;
            base[4] = b;
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

    void SetSkyGradient(std::uint8_t topR, std::uint8_t topG, std::uint8_t topB, std::uint8_t botR, std::uint8_t botG, std::uint8_t botB) {
        gSkyTopR = topR;
        gSkyTopG = topG;
        gSkyTopB = topB;
        gSkyBotR = botR;
        gSkyBotG = botG;
        gSkyBotB = botB;
        gHaveSky = true;
        EnforceWorldVisuals();
    }

    void SetMoonSize(std::uint8_t size) {
        gMoonSize = size;
        gHaveMoon = true;
        EnforceWorldVisuals();
    }

    void SetSunColor(std::uint8_t coreR, std::uint8_t coreG, std::uint8_t coreB, std::uint8_t coronaR, std::uint8_t coronaG, std::uint8_t coronaB) {
        gSunCoreR     = coreR;
        gSunCoreG     = coreG;
        gSunCoreB     = coreB;
        gSunCoronaR   = coronaR;
        gSunCoronaG   = coronaG;
        gSunCoronaB   = coronaB;
        gHaveSunColor = true;
        EnforceWorldVisuals();
    }

    void SetSunSize(float size) {
        gSunSize     = std::max(0.0f, size);
        gHaveSunSize = true;
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

        if (gHaveSky) {
            if (auto *sky = SkyBase()) {
                WriteRgbStride2(sky, gSkyTopR, gSkyTopG, gSkyTopB);
                WriteRgbStride2(sky + 6, gSkyBotR, gSkyBotG, gSkyBotB);
            }
        }

        if (gHaveMoon) {
            if (auto *moon = MoonSize()) {
                *moon = gMoonSize;
            }
        }

        if (gHaveSunColor) {
            if (auto *sun = SunColorBase()) {
                WriteRgbStride2(sun, gSunCoreR, gSunCoreG, gSunCoreB);
                WriteRgbStride2(sun + 6, gSunCoronaR, gSunCoronaG, gSunCoronaB);
            }
        }

        if (gHaveSunSize) {
            if (auto *sunSize = SunSize()) {
                // GTA stores sun size * 10; script API matches MTA's unscaled value.
                *sunSize = gSunSize * 10.0f;
            }
        }
    }
} // namespace VantaMP::SDK
