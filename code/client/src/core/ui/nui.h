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

    // Puts the interface on screen, or takes it off entirely.
    //
    // THE WHOLE VIEW, WHICH IS RARELY WHAT YOU WANT. The connect menu, the
    // console and the netgraph are all one page, so hiding the view hides all
    // three - correct while the game is still starting up, wrong the moment a
    // player is in a world where they still want a console.
    //
    // To close the menu on its own, tell the page it connected and let it close
    // itself: it owns which of its panels is up.
    void SetInterfaceVisible(bool visible);

    [[nodiscard]] bool IsInterfaceVisible();

    // The page's own notion of whether its menu is open, which it reports.
    // Drives whether the interface wants the mouse.
    [[nodiscard]] bool IsMenuOpen();

    // Tells the page a connection succeeded, so it closes its menu and leaves
    // the console and netgraph up.
    void ReportConnected(const std::string &serverName);

    // Tears the view down. Safe when nothing was ever brought up.
    void ShutdownInterface();

    // Told to the page so it can show what happened.
    void ReportConnectionState(const std::string &state, const std::string &detail);
} // namespace VantaMP::UI
