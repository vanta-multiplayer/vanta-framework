#pragma once

struct HWND__;
using HWND = HWND__ *;

namespace VantaMP::Game {
    // Takes the mouse back off the game.
    //
    // GTA acquires the mouse through DirectInput in EXCLUSIVE mode and
    // re-centres the pointer every frame for mouse-look. For a single-player
    // game with no interface of its own that is exactly right. For a client
    // that has to show a connect menu it is not: the pointer belongs to the
    // game, so the system cursor is dragged to the window centre and pinned
    // there, and there is no way to click anything.
    //
    // Mouse-look still works afterwards. The game reads its deltas from the
    // DirectInput device rather than from where the cursor is, so it does not
    // notice losing ownership of the pointer.
    //
    // Applied during startup rather than from a frame callback: it has to land
    // before the game acquires the device, and afterwards is too late.
    void ReleaseMouseCapture();

    // Tells the game it has focus, so the mouse works on the first frame.
    //
    // The game sets this itself when Windows tells it the window came to the
    // foreground - a message it never receives here, because the window already
    // existed by the time anyone was listening.
    void MarkGameForeground(bool foreground);

    // Is the user actually looking at the game's window?
    //
    // GetForegroundWindow rather than GetFocus: focus is per-thread, and the
    // question worth asking is about the user, not the thread.
    [[nodiscard]] bool HasWindowFocus(HWND window);
} // namespace VantaMP::Game
