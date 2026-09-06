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

    // The frame cap to apply once the frontend has been bypassed.
    //
    // NOT AT STARTUP, AND NOT EVERY FRAME. The game loads its own settings
    // during startup and would overwrite a value set before that; writing it
    // every frame is a global write on the render thread for a field that only
    // changes when the game touches its settings, which it no longer does.
    void SetFrameLimitOnStart(int fps);

    // True once CGame::Init3 has returned, i.e. the world exists.
    [[nodiscard]] bool IsEngineInitialised();

    // True once the frontend has been told the game is playing.
    [[nodiscard]] bool IsFrontendBypassed();

    // Lets a second client run alongside the first.
    //
    // GTA refuses to start twice, and testing a multiplayer platform means
    // running two clients on one machine constantly. Three things have to give
    // way, and doing only the obvious one is not enough:
    //
    //   the call site, because WinMain runs the instant the launcher resumes
    //   the process and a patch that only stubs the function loses the race -
    //   the second client exits before the log is even open;
    //
    //   the function too, so it is harmless if reached another way. Together
    //   these also stop the focus theft: SetForegroundWindow lives inside the
    //   branch being removed, so without them a second client yanks focus back
    //   to the first and then dies, which reads as the launcher misbehaving;
    //
    //   and the cdstream semaphore name, because two instances sharing one
    //   deadlock each other's streaming thread - the game comes up and then
    //   hangs loading, which looks nothing like an instance problem.
    void AllowMultipleInstances();
} // namespace VantaMP::Game
