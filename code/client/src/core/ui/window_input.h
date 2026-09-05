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
} // namespace VantaMP::UI
