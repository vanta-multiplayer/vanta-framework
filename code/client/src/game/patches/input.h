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

    // Keeps GTA's foreground flag matching real window focus.
    //
    // THE FLAG ONLY GATES THE MOUSE. Keeping it truthful is still right - the
    // game uses it for cursor handling and window behaviour - but it is not
    // what stops an unfocused client acting on the keyboard: GTA reads movement
    // through DirectInput, which delivers to a process whether or not its
    // window is focused, so the flag never gets a say. SuppressGameInput is
    // what actually stops it.
    //
    // Every frame, because focus changes without asking us.
    void UpdateForegroundFocus(HWND window);

    // Stops the game acting on input it should not have.
    //
    // Two cases, one mechanism. An UNFOCUSED window still receives the keyboard
    // through DirectInput, so with two clients open one keypress moves both
    // players. And while the interface has focus, typing into it would
    // otherwise also drive the player.
    //
    // Consuming window messages does not help - DirectInput never reaches the
    // window procedure. Clearing the pad is where both paths meet.
    void SuppressGameInput(bool suppress);
} // namespace VantaMP::Game
