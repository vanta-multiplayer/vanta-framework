#pragma once

namespace VantaMP::Game {
    // Hooks LoadSettings so GTA's own file cannot overwrite the mix,
    // and applies the stored volumes once the audio engine exists.
    void ApplyAudioPatches();

    // Live. Master scales SFX and radio, then both are written 0-64.
    void SetVolumeSettings(float master, float sfx, float radio);

    // Pushes the stored mix into CMenuManager and AudioEngine.
    void ApplyGameVolumes();
} // namespace VantaMP::Game
