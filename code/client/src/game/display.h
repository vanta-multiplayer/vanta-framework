#pragma once

#include <cstdint>

struct HWND__;
using HWND = HWND__ *;
struct _D3DPRESENT_PARAMETERS_;
using D3DPRESENT_PARAMETERS = _D3DPRESENT_PARAMETERS_;

namespace VantaMP::Game {
    struct DisplaySettings {
        // 0 means "whatever the game asked for", which is how this behaves
        // before anything has configured it.
        std::uint32_t width  = 0;
        std::uint32_t height = 0;

        // Borderless covering the monitor, rather than a window with a frame.
        bool borderless = true;
    };

    void SetDisplaySettings(const DisplaySettings &settings);
    [[nodiscard]] const DisplaySettings &GetDisplaySettings();

    // Rewrites the parameters GTA is about to create its device with.
    //
    // WINDOWED, ALWAYS. Exclusive fullscreen on this engine regularly takes the
    // display mode and then never presents, which is indistinguishable from a
    // hang; and alt-tabbing between two clients - which is most of what testing
    // a multiplayer platform consists of - either fails or costs a device
    // reset each way. A borderless window covering the monitor looks the same
    // to a player and has neither problem.
    //
    // Done HERE rather than by replacing the game's device selection. GTA picks
    // its mode through RenderWare, which means RenderWare types, which means
    // plugin-sdk - and plugin-sdk is confined to one translation unit because
    // its headers fight the framework's. D3DPRESENT_PARAMETERS is the same
    // decision expressed in plain Direct3D, so it needs none of that and is not
    // tied to one build of the game.
    void ApplyToPresentParameters(D3DPRESENT_PARAMETERS *parameters, HWND window);

    // Sizes and positions the game's window to match what was asked for.
    //
    // The window has to be styled before it is measured: the style decides how
    // much larger the outer window is than its client area, so doing it the
    // other way round leaves the client area a title bar short.
    //
    // CALLED REPEATEDLY, NOT ONCE. GTA sets its own window style during device
    // setup, after CreateDevice returns, so a single application at that point
    // is silently undone - the log says "windowed" while the window on screen
    // is still the game's borderless popup. Re-asserting from the frame loop is
    // what makes the setting stick; it is a no-op once the style already
    // matches, so the cost is a comparison per frame.
    void ApplyWindowStyle(HWND window);
} // namespace VantaMP::Game
