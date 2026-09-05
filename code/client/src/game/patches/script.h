#pragma once

namespace VantaMP::Game {
    // Stops GTA's mission script, and takes over its per-frame call.
    //
    // San Andreas is driven by a compiled mission script. None of it applies in
    // multiplayer: it would start missions, teleport the player and generally
    // fight the server for control of the same world. So the SCM is not loaded
    // and its interpreter is stubbed.
    //
    // Its per-frame call is then reused rather than removed, because the moment
    // scripts would have begun running is exactly the moment the world is
    // finished and a player can exist in it. That call becomes the client's
    // frame tick.
    void ApplyScriptPatches();

    // Runs once per frame, on the game thread. Higher layers hang their own
    // per-frame work off this rather than adding another hook to the most
    // fragile part of startup.
    void SetFrameCallback(void (*callback)());
} // namespace VantaMP::Game
