#pragma once

namespace VantaMP::Game {
    struct SpawnPoint {
        float x = 2488.562f;
        float y = -1666.865f;
        float z = 12.8757f;
    };

    // Stops GTA's mission script and creates the local player in its place.
    //
    // San Andreas is driven by a compiled mission script. In multiplayer none
    // of it applies: it would start missions, teleport the player and generally
    // fight the server for control of the same world. So it is not loaded and
    // its interpreter is stubbed.
    //
    // Its per-frame call is then reused, because the moment scripts would have
    // begun running is exactly the moment the world is finished and a player
    // can exist in it. That call becomes Vanta's own frame tick.
    void ApplyScriptPatches();

    // Where the local player is put when it is created.
    //
    // A placeholder until the server says otherwise - the player is created
    // behind a black screen precisely because this position means nothing.
    void SetSpawnPoint(const SpawnPoint &point);

    [[nodiscard]] bool IsPlayerSpawned();

    // Hands the screen back after the startup fade.
    //
    // SECONDS, NOT MILLISECONDS. The game divides by this value when stepping
    // the fade alpha, so passing 500 for "half a second" asks for a
    // five-hundred-second fade - which does clear the screen, at about a
    // fiftieth of a percent per frame, and is indistinguishable from a reveal
    // that never happens.
    //
    // Whether the player may see the world is session state, not a decision for
    // the patch layer, so nothing here calls this on its own.
    void FadeIn(float durationSeconds = 0.5f);
    void FadeOut(float durationSeconds = 0.0f);

    // Runs once per frame, from the script call above. Game thread.
    void SetFrameCallback(void (*callback)());
} // namespace VantaMP::Game
