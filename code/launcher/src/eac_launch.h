#pragma once

namespace VantaMP::Launcher {
    // Force cwd to the directory containing VantaMPLauncher.exe.
    // Framework::Project records GetCurrentDirectory as the client runtime path.
    [[nodiscard]] bool PinWorkingDirectoryToLauncher();

    // If this process was not started through start_protected_game.exe,
    // relaunch via that Anti-Cheat bootstrapper and ask the caller to exit.
    //
    // Returns true  -> continue into GTA PE loading (already protected, or EAC unavailable).
    // Returns false -> bootstrapper was started; caller should exit(0).
    [[nodiscard]] bool EnsureEacProtectedProcess();
} // namespace VantaMP::Launcher
