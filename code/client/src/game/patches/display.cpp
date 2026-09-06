#include "game/patches/display.h"

#include "sdk/patterns.h"

#include <utils/safe_win32.h>

#include <d3d9.h>

#include <logging/logger.h>
#include <utils/hooking/hooking.h>
#include <utils/hooking/hooking_patterns.h>

#include <algorithm>

namespace VantaMP::Game {
    namespace {
        DisplaySettings gSettings;

        // What the device ended up being created at.
        SIZE gResolved {0, 0};

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

            // FIT THE OUTER WINDOW, NOT THE CLIENT AREA.
            //
            // A windowed client asking for a client area the size of the screen
            // needs an OUTER window bigger than the screen - a title bar and
            // borders taller than the display it has to live on. Windows then
            // hands back a smaller client area than was asked for, and the
            // device is created at one size while everything measuring the
            // window sees another. The interface is sized from the window and
            // D3D presents at the device's size, and the two disagreeing is
            // fatal.
            //
            // So the request is capped by what a window can actually contain.
            RECT frame {0, 0, 0, 0};
            AdjustWindowRect(&frame, static_cast<DWORD>(GetWindowLongW(window, GWL_STYLE)), FALSE);

            const LONG chromeWidth  = (frame.right - frame.left);
            const LONG chromeHeight = (frame.bottom - frame.top);

            size.cx = std::min(size.cx, screenWidth - chromeWidth);
            size.cy = std::min(size.cy, screenHeight - chromeHeight);

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

        // WHAT THE PLAYER PICKED IN THE GAME'S OWN DIALOG WINS.
        //
        // GTA asks for a device and a video mode at startup, and whatever is
        // chosen there is already in these parameters. Overwriting it with a
        // configured size means the dialog does nothing: a player selects
        // 1280x720, gets whatever the file said, and has no way to tell why.
        //
        // The config is a fallback for when nothing was chosen - a first run,
        // or a borderless client that covers the monitor regardless.
        const bool chosen = parameters->BackBufferWidth > 0 && parameters->BackBufferHeight > 0;

        const auto size = (chosen && !gSettings.borderless) ? SIZE {static_cast<LONG>(parameters->BackBufferWidth), static_cast<LONG>(parameters->BackBufferHeight)} : ResolveSize(window);

        gResolved = size;

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

        // The size the device was actually created at, so the window's client
        // area matches the back buffer. Anything else stretches the image or
        // leaves the interface laid out for a size nothing is rendering at.
        const auto size = gResolved.cx > 0 ? gResolved : ResolveSize(window);

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
    void SetFrameLimit(int fps) {
        const auto rsGlobal = SDK::gPatterns.RsGlobal;

        if (!rsGlobal) {
            return;
        }

        // maxFPS at +12, after appName, maximumWidth and maximumHeight. A plain
        // field the game's own limiter reads each frame, so writing it is the
        // whole of the work.
        hook::put<std::int32_t>(rsGlobal + 12, fps);
    }

    void ApplyWidescreenPatches() {
        const auto &p = SDK::gPatterns;

        // The resolution gate. Two signed-less-than jumps become unconditional
        // ones, and three comparisons that feed them are removed.
        //
        // UNLOCKING MODES IS NOT THE SAME AS RENDERING AT ONE: this only makes
        // the wider modes selectable. What the game then draws is still laid out
        // for 4:3 until ApplyAspectRatio corrects it.
        hook::put<std::uint32_t>(p.Widescreen__Check1, 0x9090587Du);
        hook::put<std::uint32_t>(p.Widescreen__Check2, 0x9090127Du);
        hook::nop(p.Widescreen__Nop1, 2);
        hook::nop(p.Widescreen__Nop2, 2);
        hook::nop(p.Widescreen__Nop3, 2);

        // NEUTERED, NOT CORRECTED. This recomputes the aspect ratio every frame
        // from the game's own 4:3 assumption, so a value written while it still
        // runs is overwritten before anyone sees it - which looks like the fix
        // working and then reverting.
        hook::return_function(p.CDraw__CalculateAspect);

        Framework::Logging::GetLogger("Display")->info("Widescreen modes unlocked");
    }

    void ApplyAspectRatio(HWND window) {
        const auto &p = SDK::gPatterns;

        // The window's real shape. Taken from the resolved size rather than the
        // configured one, because a request of 0 means "the monitor" and a
        // request larger than the screen is capped.
        const auto size = ResolveSize(window);

        if (size.cx <= 0 || size.cy <= 0) {
            // Nothing sensible to derive from. Left at whatever the game
            // computed rather than writing a ratio of zero, which the
            // projection would divide by.
            return;
        }

        const float ratio = static_cast<float>(size.cx) / static_cast<float>(size.cy);

        hook::put<float>(p.CDraw__AspectRatio, ratio);

        // AND THE HUD, WHICH DOES NOT FOLLOW THE PROJECTION.
        //
        // Correcting the camera alone leaves every HUD element laid out for 4:3:
        // the radar and bars sit inset from the corners rather than at them. The
        // game scales all of it by one multiplier with the old shape baked in,
        // so re-deriving that multiplier moves everything at once.
        constexpr float kFourThirds = 4.0f / 3.0f;

        // 1/448 as shipped, and confirmed by reading the executable. Named
        // rather than written inline because the expression below is "the game's
        // own value, re-based from 4:3 to ours" - not an arbitrary number.
        constexpr float kShippedHudMultiplier = 0.002232143f;

        hook::put<float>(p.CHud__AspectMultiplier, kShippedHudMultiplier / kFourThirds * ratio);

        // The crosshair is a fixed pixel size, so a wider projection stretches it
        // into an ellipse. Scaled by the INVERSE: it has to shrink horizontally
        // as the view widens, which is the opposite of what the layout
        // multiplier does.
        constexpr float kShippedCrosshairScale = 192.0f;

        hook::put<float>(p.CCamera__CrosshairScale, kShippedCrosshairScale * kFourThirds / ratio);

        Framework::Logging::GetLogger("Display")->info("Aspect ratio {:.4f} for {}x{}; HUD and crosshair rescaled", ratio, size.cx, size.cy);
    }
} // namespace VantaMP::Game
