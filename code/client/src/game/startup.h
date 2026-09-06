#pragma once

namespace VantaMP::Game {
    // Identifies the host executable, then applies every patch and hook the
    // client needs before WinMain continues.
    //
    // ORDER IS THE WHOLE POINT. Later items assume earlier ones have already
    // rewritten the game - a slot table that has not been expanded, a world
    // that has already spawned traffic, a mouse the game already owns. The
    // sequence lives here so the inject entry does not have to know any of it.
    void Start();
} // namespace VantaMP::Game
