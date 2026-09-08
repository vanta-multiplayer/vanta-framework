#include "host_modules.h"

#include "core/globals.h"

#include <logging/logger.h>

namespace VantaMP::Eac {
    void InstallHostModuleRedirects() {
        // eax/ogg/vorbis are Vanta-built (or Xiph-built) DLLs shipped next to the
        // launcher and hashed in base.bin. The PE IAT resolves them from the
        // application directory — do NOT MinHook LoadLibrary*: after BeginSession
        // the AC client probes loader hooks and ends the session with
        // EOS_ACCCAR_InvalidMessage ("Corrupt or invalid backend message").
        if (!VantaMP::Core::gProjectPath.empty()) {
            Framework::Logging::GetLogger("EAC")->info(
                "Host modules expected beside launcher (no LoadLibrary hooks): {}", VantaMP::Core::gProjectPath);
        }
    }
} // namespace VantaMP::Eac
