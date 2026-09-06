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

    // A point in the world, as pixels on this frame's backbuffer.
    //
    // visible is false when the point is behind the camera or the game refuses
    // the projection. z is the camera-space depth, useful for a cheap "in
    // front" test after the fact.
    struct ScreenPoint {
        bool visible = false;

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    [[nodiscard]] ScreenPoint WorldToScreen(float x, float y, float z);
} // namespace VantaMP::SDK
