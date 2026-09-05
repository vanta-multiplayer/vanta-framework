#pragma once

namespace VantaMP::Game {
    // Switches off the single-player gameplay a server should be deciding.
    //
    // San Andreas keeps a great deal of state about a story nobody here is
    // playing: stats that drift as you run and swim, respect and skill
    // announcements, a hunger nag, garages, replays, an idle camera. In
    // multiplayer that state is the server's to own, and the announcements
    // describe a game that is not happening.
    //
    // It also fixes two frame-rate bugs. GTA was written for a hardware
    // generation that could not exceed its own frame cap; uncapped, its
    // frame-time-dependent physics start behaving differently on faster
    // machines, which in multiplayer means two players doing the same thing
    // getting different results.
    void ApplyGameplayPatches();
} // namespace VantaMP::Game
