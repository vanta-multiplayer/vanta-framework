#pragma once

namespace VantaMP::Shared {
    // Registry sub-key the Rockstar Games Launcher installs the title under, below
    // HKLM\SOFTWARE\Rockstar Games (WOW6432Node on a 64-bit host).
    inline constexpr const wchar_t *kRockstarTitleKey = L"GTA: San Andreas";

    inline constexpr const wchar_t *kGameExecutable = L"gta_sa.exe";
    inline constexpr const char *kGameName          = "Grand Theft Auto: San Andreas";

    // BOTH SIDES MUST AGREE ON THESE, so they are declared once.
    //
    // The framework refuses a connection whose build token does not match, and
    // the token is derived from the game name, the game version and the mod
    // slug. Declaring them separately in the client and the server is how a
    // client ends up kicked for "wrong client version" against a server built
    // from the same commit - which is exactly what happened when the slug read
    // "vantamp" on one side and "vantamp_server" on the other.

    // The GTA:SA build Vanta targets. Not the mod's version - that is
    // VantaMP::Version::rel, which the build stamps.
    inline constexpr const char *kGameVersion = "1.0.0";

    // Identifies the mod itself. One value: a server does not run a different
    // mod to the client that connects to it.
    inline constexpr const char *kModSlug = "vantamp";
} // namespace VantaMP::Shared
