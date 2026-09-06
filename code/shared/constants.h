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

    // Where crash reports are sent.
    //
    // A Sentry DSN is a WRITE-ONLY client key: it can submit events and cannot
    // read them, which is why it is designed to ship inside client binaries
    // rather than be treated as a secret. Anyone holding the client can post
    // events to the project, so abuse is handled with Sentry's rate limits and
    // inbound filters, not by hiding this string.
    //
    // Empty disables crash reporting entirely - the framework skips
    // initialisation when there is no DSN, and the local report in
    // core/diagnostics/crash_report.cpp keeps working either way.
    inline constexpr const char *kSentryDSN = "https://f29af63e37c7108203dc74a627f52091@o4512036562206720.ingest.de.sentry.io/4512036569546832";

    // Separates real player crashes from ones we caused ourselves while
    // building. Without this every local test lands in the same list as
    // production and the list stops being worth reading.
#ifdef NDEBUG
    inline constexpr const char *kSentryEnvironment = "production";
#else
    inline constexpr const char *kSentryEnvironment = "development";
#endif
} // namespace VantaMP::Shared
