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
} // namespace VantaMP::SDK
