#pragma once

namespace VantaMP::SDK {
    // Which way the local player's camera is pointing, in radians.
    //
    // GTA resolves stick input against this rather than against the ped's
    // heading, so it has to travel with the input it will be interpreted
    // against - see PlayerEntity::cameraRotation.
    [[nodiscard]] float ReadLocalCameraRotation();

    // The camera rotation a remote player's ped is processed under.
    //
    // Stored per slot rather than written straight to the game: the global is
    // shared, so it can only hold one player's value at a time and is swapped
    // in around each ped's processing.
    void SetRemoteCameraRotation(int slot, float rotation);

    [[nodiscard]] float GetRemoteCameraRotation(int slot);
} // namespace VantaMP::SDK
