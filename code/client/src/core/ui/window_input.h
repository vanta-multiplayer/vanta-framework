#pragma once

struct HWND__;
using HWND = HWND__ *;

namespace VantaMP::UI {
    // Sends the game's window messages to the interface as well.
    //
    // CEF has no window of its own - it paints into a texture the client draws.
    // So the only way it learns about a click or a keystroke is for whoever DOES
    // own the window to hand the message over, which means subclassing the
    // game's window procedure.
    //
    // MESSAGES ARE FORWARDED, NOT STOLEN. The game still receives everything;
    // this only copies input to CEF as it passes. Swallowing messages while a
    // menu is up sounds tidier and is how a client ends up with a game that has
    // silently stopped hearing about alt-tab, resize or close.
    //
    // The game's own reading of input is a separate matter: it takes movement
    // through DirectInput, which never touches the window procedure at all.
    void InstallWindowHook(HWND window);

    void RemoveWindowHook();

    // Keeps the pointer inside the game's window.
    //
    // Without this the cursor walks off the edge mid-game and clicks whatever
    // is behind - another window, the desktop, the taskbar. Confined to the
    // CLIENT area rather than the window, so a windowed client's title bar is
    // not part of the play area.
    //
    // RELEASED THE MOMENT FOCUS IS LOST, or alt-tabbing would leave the pointer
    // trapped in a window the player is no longer looking at, with no way to
    // reach anything else. That is a mod holding the mouse hostage, and it is
    // exactly the behaviour people uninstall over.
    //
    // Cheap to call every frame: it compares the focus state and only touches
    // the system when that changes.
    void UpdateCursorClip(HWND window);
} // namespace VantaMP::UI
