#pragma once

namespace VantaMP::Shared {
    // Registry sub-key the Rockstar Games Launcher installs the title under, below
    // HKLM\SOFTWARE\Rockstar Games (WOW6432Node on a 64-bit host).
    inline constexpr const wchar_t *kRockstarTitleKey = L"GTA: San Andreas";

    inline constexpr const wchar_t *kGameExecutable = L"gta_sa.exe";
    inline constexpr const char *kGameName          = "Grand Theft Auto: San Andreas";
} // namespace VantaMP::Shared
