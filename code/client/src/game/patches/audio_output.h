#pragma once

#include <string>

namespace VantaMP::Game {
    // DirectSoundCreate is hooked so GTA opens this endpoint instead of
    // the system default. The device is created once, during boot, so a
    // change after that is stored and used on the next launch.
    void SetGameOutputDevice(const std::string &deviceId);
    void InstallGameOutputHook();
} // namespace VantaMP::Game
