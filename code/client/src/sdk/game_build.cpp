#include "game_build.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>

namespace VantaMP::SDK {
    namespace {
        // PE timestamp + SizeOfImage, both read from the mapped image.
        //
        // File size is deliberately not part of this. A repacked, patched or
        // launcher-decrypted copy differs on disk while the image the loader
        // built is still the same build, and the Rockstar one is never on disk
        // in the form it runs as.
        struct BuildFingerprint {
            GameBuild build;
            std::uint32_t timeDateStamp;
            std::uint32_t sizeOfImage;
        };

        // Measured, not copied. Only 1.0 US is filled in because it is the only
        // build whose numbers we have verified ourselves; the others are
        // recognised by their scans resolving, and get named here as we confirm
        // them rather than on the strength of a value found on a forum.
        constexpr BuildFingerprint kFingerprints[] = {
            {GameBuild::SanAndreas10US, 0x427101CAu, 0x01177000u},
        };
    } // namespace

    const char *GameBuildInfo::Name() const noexcept {
        switch (build) {
        case GameBuild::SanAndreas10US: return "GTA:SA 1.0 US";
        case GameBuild::SanAndreas101: return "GTA:SA 1.01";
        case GameBuild::SanAndreasSteam: return "GTA:SA (Steam)";
        case GameBuild::SanAndreasRockstar: return "GTA:SA (Rockstar Games Launcher)";
        case GameBuild::Unknown: break;
        }

        return "GTA:SA (unrecognised build)";
    }

    GameBuildInfo IdentifyGameBuild() {
        GameBuildInfo info {};

        // The executable, not this DLL.
        const auto module = reinterpret_cast<const std::uint8_t *>(GetModuleHandleW(nullptr));

        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(module);
        const auto *nt  = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(module + dos->e_lfanew);

        info.imageBase     = reinterpret_cast<std::uintptr_t>(module);
        info.timeDateStamp = nt->FileHeader.TimeDateStamp;
        info.sizeOfImage   = nt->OptionalHeader.SizeOfImage;

        for (const auto &candidate : kFingerprints) {
            if (candidate.timeDateStamp == info.timeDateStamp && candidate.sizeOfImage == info.sizeOfImage) {
                info.build = candidate.build;
                break;
            }
        }

        // Said at info either way. An unrecognised build is not an error - the
        // scans decide that - but it is the first thing worth knowing when one
        // of them later fails to resolve.
        return info;
    }
} // namespace VantaMP::SDK
