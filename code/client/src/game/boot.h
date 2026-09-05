#pragma once

namespace VantaMP::Game {
    // Takes GTA from process start to a running world, without its menus.
    //
    // Single-player San Andreas boots into a title sequence, a set of loading
    // screens and a main menu, and only reaches the world when a human picks
    // "New Game". A multiplayer client has its own connect flow and needs the
    // world up before any of it means anything, so the frontend is bypassed
    // rather than driven.
    //
    // BYPASSED, NOT DELETED. The menu's state machine still runs and still gets
    // told the game is playing; what is removed is its drawing, its input and
    // its ability to pause. Skipping the machinery outright leaves the game
    // waiting on transitions that never happen - which looks exactly like a
    // hang, and was how several earlier attempts at this failed.
    //
    // The copyright screen is the deliberate exception: it is the legal notice
    // the game ships with, and a multiplayer client has no business removing
    // it. Only the loading artwork after it goes.
    void ApplyBootPatches();

    // True once CGame::Init3 has returned, i.e. the world exists.
    [[nodiscard]] bool IsEngineInitialised();

    // True once the frontend has been told the game is playing.
    [[nodiscard]] bool IsFrontendBypassed();
} // namespace VantaMP::Game
