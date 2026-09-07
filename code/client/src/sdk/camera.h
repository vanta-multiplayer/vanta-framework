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

    // Where the player is listening from this frame: camera source for pan,
    // camera front/up for stereo. GTA is Z-up; the mixer needs that, not the
    // +Y default.
    struct ListenerCamera {
        bool valid = false;

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        float forwardX = 0.0f;
        float forwardY = 0.0f;
        float forwardZ = -1.0f;

        float upX = 0.0f;
        float upY = 0.0f;
        float upZ = 1.0f;
    };

    [[nodiscard]] ListenerCamera ReadListenerCamera();
} // namespace VantaMP::SDK
