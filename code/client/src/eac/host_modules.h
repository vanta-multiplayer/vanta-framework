#pragma once

namespace VantaMP::Eac {
    // Documents that eax/ogg/vorbis live next to the launcher (Vanta/Xiph builds
    // in base.bin). Does not hook LoadLibrary* — that trips EAC after BeginSession.
    void InstallHostModuleRedirects();
} // namespace VantaMP::Eac
