#pragma once

namespace VantaMP::Game {
    // The screen fade, which is how the client hides a world the player has no
    // business seeing yet.
    //
    // SECONDS, NOT MILLISECONDS. The game divides by this value when stepping
    // the fade alpha, so passing 500 for "half a second" asks for a
    // five-hundred-second fade - which clears the screen at a fiftieth of a
    // percent per frame and is indistinguishable from a reveal that never
    // happens.
    //
    // Whether the player may see the world is session state, so nothing here
    // decides to reveal anything on its own.
    void FadeIn(float durationSeconds = 0.5f);
    void FadeOut(float durationSeconds = 0.0f);
} // namespace VantaMP::Game
