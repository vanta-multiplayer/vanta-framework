#pragma once

#include <cstdint>

namespace VantaMP::SDK {
    // Server clock onto CClock hours/minutes.
    void SetGameClock(std::uint8_t hour, std::uint8_t minute);
    void SetMinuteDuration(std::uint32_t millisecondsPerGameMinute);

    // CTimer::ms_fTimeScale. Set stores the authority; Enforce rewrites it
    // every frame so a local poke cannot speed the sim up.
    void SetGameSpeed(float speed);
    void EnforceGameSpeed();
    [[nodiscard]] float GameSpeed();

    // CWeather::ForceWeatherNow.
    void SetWeather(std::uint8_t weatherId);

    // Global gravity float. Pulsed like game speed.
    void SetGravity(float gravity);
    void EnforceGravity();

    // Optional visual overrides. Only pulsed after the server has set them.
    void SetFarClip(float distance);
    void SetFogDistance(float distance);
    void SetRainLevel(float level);
    void SetWaveHeight(float height);
    void SetSkyGradient(std::uint8_t topR, std::uint8_t topG, std::uint8_t topB, std::uint8_t botR, std::uint8_t botG, std::uint8_t botB);
    void SetMoonSize(std::uint8_t size);
    void SetSunColor(std::uint8_t coreR, std::uint8_t coreG, std::uint8_t coreB, std::uint8_t coronaR, std::uint8_t coronaG, std::uint8_t coronaB);
    void SetSunSize(float size);
    void SetWaterColor(float r, float g, float b, float a);
    void SetHeatHaze(std::uint8_t intensity, std::uint8_t randomShift, std::uint16_t speedMin, std::uint16_t speedMax, std::int16_t scanX,
                     std::int16_t scanY, std::uint16_t renderX, std::uint16_t renderY, bool insideBuilding);
    void EnforceWorldVisuals();
} // namespace VantaMP::SDK
