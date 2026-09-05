#include "display.h"

#include "sdk/patterns.h"

#include <utils/safe_win32.h>

#include <d3d9.h>

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

#include <algorithm>

namespace VantaMP::Game {
    namespace {
        DisplaySettings gSettings;

        // The monitor the window is on, so a size is centred where the player
        // actually is. Window coordinates span the whole virtual desktop, so a
        // monitor left of the primary one has a negative origin and anything
        // placed at 0,0 lands on the primary regardless.
        RECT MonitorRectFor(HWND window) {
            MONITORINFO info {sizeof(MONITORINFO)};

            if (const auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST); monitor && GetMonitorInfoW(monitor, &info)) {
                return info.rcMonitor;
            }

            return RECT {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        }

        // What the player asked for, resolved against the display they are on.
        // A zero means "unset", and a request larger than the monitor is capped
        // rather than honoured - a window bigger than the screen cannot be
        // moved or closed.
        SIZE ResolveSize(HWND window) {
            const auto monitor = MonitorRectFor(window);
            const LONG screenWidth  = monitor.right - monitor.left;
            const LONG screenHeight = monitor.bottom - monitor.top;

            SIZE size {static_cast<LONG>(gSettings.width), static_cast<LONG>(gSettings.height)};

            if (size.cx <= 0 || size.cy <= 0 || gSettings.borderless) {
                return SIZE {screenWidth, screenHeight};
            }

            size.cx = std::min(size.cx, screenWidth);
            size.cy = std::min(size.cy, screenHeight);
            return size;
        }
    } // namespace

    void SetDisplaySettings(const DisplaySettings &settings) {
        gSettings = settings;
    }

    const DisplaySettings &GetDisplaySettings() {
        return gSettings;
    }

    void ApplyToPresentParameters(D3DPRESENT_PARAMETERS *parameters, HWND window) {
        if (!parameters) {
            return;
        }

        const auto size = ResolveSize(window);

        parameters->Windowed         = TRUE;
        parameters->BackBufferWidth  = static_cast<UINT>(size.cx);
        parameters->BackBufferHeight = static_cast<UINT>(size.cy);

        // A fullscreen request carries a refresh rate and a presentation
        // interval that are only meaningful when the device owns the display.
        // Leaving them set on a windowed device is rejected outright by D3D9.
        parameters->FullScreen_RefreshRateInHz = 0;

        // AND TELL THE ENGINE, or it renders at a size nothing agrees with.
        //
        // GTA keeps what it thinks the screen is in RsGlobal, and close to five
        // hundred sites read it - the projection, every HUD coordinate, the
        // menus, the mouse. Changing the back buffer without changing this
        // leaves the engine drawing 1920x1200 into a 1280x720 surface, which is
        // not a stretched image but a crash.
        //
        // Only when the size actually differs from what the game chose: at
        // startup RsGlobal has not been populated yet, and writing to it before
        // the engine does is pointless rather than harmful.
        if (const auto rsGlobal = SDK::gPatterns.RsGlobal; rsGlobal) {
            hook::put<std::int32_t>(rsGlobal + 4, static_cast<std::int32_t>(size.cx));
            hook::put<std::int32_t>(rsGlobal + 8, static_cast<std::int32_t>(size.cy));
        }

        Framework::Logging::GetLogger("Display")->info("Device forced windowed at {}x{}", size.cx, size.cy);
    }

    void ApplyWindowStyle(HWND window) {
        if (!window) {
            return;
        }

        const auto monitor = MonitorRectFor(window);
        const auto size    = ResolveSize(window);

        auto style = GetWindowLongW(window, GWL_STYLE);

        // WS_POPUP HAS TO GO FOR A WINDOWED CLIENT, and it is the bit that is
        // easy to miss. GTA creates its window as a popup, and a popup has no
        // caption no matter which other bits are set - so adding WS_CAPTION to
        // it changes nothing and the window stays borderless while every log
        // line claims otherwise.
        //
        // The two styles are therefore built rather than adjusted: whatever the
        // game chose, the result is exactly one of these.
        if (gSettings.borderless) {
            style &= ~(WS_BORDER | WS_DLGFRAME | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
            style |= WS_POPUP;
        }
        else {
            style &= ~WS_POPUP;
            style |= WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;

            // No thick frame: the client area is sized to match the back buffer
            // and dragging the edge would not resize the buffer with it, so the
            // image would simply be stretched into whatever the player dragged.
            style &= ~WS_THICKFRAME;
        }

        // NOTHING TO DO IF IT IS ALREADY RIGHT. This runs every frame, and
        // re-styling a window that already matches makes it flicker.
        //
        // Compared against the style that was just computed rather than against
        // a single bit standing in for "correct". An earlier version tested
        // WS_POPUP on the assumption that GTA creates a popup window; it does
        // not - the window arrives as 0x04CF0000, already captioned - so that
        // test passed immediately and the window was never sized to match the
        // back buffer. The game then rendered 1280x720 into a surface of its own
        // choosing and died.
        const auto currentStyle = GetWindowLongW(window, GWL_STYLE);
        RECT currentRect {};
        GetWindowRect(window, &currentRect);

        SetWindowLongW(window, GWL_STYLE, style);

        // ADJUSTED, SO THE REQUESTED SIZE IS THE CLIENT AREA. SetWindowPos sizes
        // the outer window, borders included, so passing the wanted size
        // straight in yields a client area that much smaller - and the game
        // then renders at a size every screen-space calculation disagrees with.
        RECT target {0, 0, size.cx, size.cy};
        AdjustWindowRect(&target, static_cast<DWORD>(style), FALSE);

        const int outerWidth  = target.right - target.left;
        const int outerHeight = target.bottom - target.top;

        const int x = gSettings.borderless ? monitor.left : monitor.left + ((monitor.right - monitor.left) - outerWidth) / 2;
        const int y = gSettings.borderless ? monitor.top : monitor.top + ((monitor.bottom - monitor.top) - outerHeight) / 2;

        const bool unchanged = currentStyle == style && currentRect.left == x && currentRect.top == y && (currentRect.right - currentRect.left) == outerWidth && (currentRect.bottom - currentRect.top) == outerHeight;

        if (unchanged) {
            return;
        }

        SetWindowPos(window, nullptr, x, y, outerWidth, outerHeight, SWP_NOZORDER | SWP_FRAMECHANGED);

        // Logged on change rather than every frame: a line here after startup
        // means the game re-styled its window behind us, which is worth seeing.
        Framework::Logging::GetLogger("Display")->info("Window {} at {}x{}, origin {},{}", gSettings.borderless ? "borderless" : "windowed", size.cx, size.cy, x, y);
    }
} // namespace VantaMP::Game
