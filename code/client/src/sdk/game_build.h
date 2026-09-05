#pragma once

#include <cstdint>

namespace VantaMP::SDK {
    // Which San Andreas the client was loaded into.
    //
    // IDENTIFICATION, NOT GATING. Vanta's old client refused to run on anything
    // but 1.0 US, and it had to: it addressed the game with a table of literal
    // addresses, and pointing that table at another build corrupts memory at
    // effectively random locations.
    //
    // Addresses now come from signature scans, which find their own way around
    // whichever build is loaded, so an unrecognised executable is no longer
    // dangerous by itself - a scan either resolves or fails loudly at startup.
    // What the build tells us is narrower and still worth knowing: which
    // pattern table to seed from, what to put in a crash report, and which
    // behaviours differ between builds in ways no signature can express.
    enum class GameBuild {
        Unknown,

        // The 2005 retail build almost every GTA:SA tool targets, and the one
        // Vanta's own behaviour was developed and verified against.
        SanAndreas10US,

        SanAndreas101,
        SanAndreasSteam,

        // The Rockstar Games Launcher build. Its .text is encrypted on disk by
        // MTLX.dll, so it is reached through the launcher's image snapshot
        // rather than by loading the file directly.
        SanAndreasRockstar,
    };

    struct GameBuildInfo {
        GameBuild build = GameBuild::Unknown;

        std::uint32_t timeDateStamp = 0;
        std::uint32_t sizeOfImage   = 0;
        std::uintptr_t imageBase    = 0;

        // The name to show a human - a log line, a crash report, the connect UI.
        [[nodiscard]] const char *Name() const noexcept;

        // True when this is a build we have actually run against, as opposed to
        // one the scans merely happen to resolve on. Used to decide whether to
        // say so out loud, never to refuse to start.
        [[nodiscard]] bool IsKnown() const noexcept {
            return build != GameBuild::Unknown;
        }
    };

    // Reads the PE headers of the host executable.
    //
    // Works off the mapped image rather than the file on disk, so it is correct
    // for a repacked or launcher-decrypted copy whose file no longer matches
    // what is in memory - which is the normal case for the Rockstar build.
    [[nodiscard]] GameBuildInfo IdentifyGameBuild();
} // namespace VantaMP::SDK
