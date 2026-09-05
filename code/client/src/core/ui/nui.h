#pragma once

#include <string>

namespace VantaMP::UI {
    // Vanta's own interface: the connect menu and the console.
    //
    // Brought up as soon as the renderer exists, which is well before the world
    // does - the point is that a player has something to look at and something
    // to click while the game is still loading behind it.
    //
    // The page is the one from the previous client, unchanged. It talks to a
    // `__vantaNui` global that the old client injected; the framework's CEF
    // offers `callEvent(name, payload)` instead, so a small shim maps one onto
    // the other rather than the page being rewritten to suit whichever host it
    // happens to be running in.
    void BringUpInterface(int width, int height);

    // Shows or hides the menu. Called when the player connects or disconnects,
    // and by the page itself when it wants the screen back.
    void SetMenuVisible(bool visible);

    [[nodiscard]] bool IsMenuVisible();

    // Tears the view down. Safe when nothing was ever brought up.
    void ShutdownInterface();

    // Told to the page so it can show what happened.
    void ReportConnectionState(const std::string &state, const std::string &detail);
} // namespace VantaMP::UI
