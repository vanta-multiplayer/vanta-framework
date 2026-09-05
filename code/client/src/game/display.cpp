#include "display.h"

#include <utils/safe_win32.h>

#include <d3d9.h>

#include <logging/logger.h>

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

        Framework::Logging::GetLogger("Display")->info("Device forced windowed at {}x{}", size.cx, size.cy);
    }

    void ApplyWindowStyle(HWND window) {
        if (!window) {
            return;
        }

        const auto monitor = MonitorRectFor(window);
        const auto size    = ResolveSize(window);

        auto style = GetWindowLongW(window, GWL_STYLE);

        style = gSettings.borderless ? (style & ~(WS_BORDER | WS_DLGFRAME | WS_THICKFRAME | WS_CAPTION))
                                     : (style | WS_BORDER | WS_DLGFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);

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

        SetWindowPos(window, nullptr, x, y, outerWidth, outerHeight, SWP_NOZORDER | SWP_FRAMECHANGED);

        Framework::Logging::GetLogger("Display")->info("Window {} at {}x{}, origin {},{}", gSettings.borderless ? "borderless" : "windowed", size.cx, size.cy, x, y);
    }
} // namespace VantaMP::Game
