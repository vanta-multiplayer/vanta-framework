#include "game/patches/display.h"

#include "core/client_config.h"
#include "sdk/patterns.h"

#include <utils/safe_win32.h>

#include <d3d9.h>

#include <logging/logger.h>
#include <utils/hooking/hooking.h>
#include <utils/hooking/hooking_patterns.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace VantaMP::Game {
    namespace {
        DisplaySettings gSettings;

        // What the device ended up being created at.
        SIZE gResolved {0, 0};

        // Frames left to re-assert chrome after a live apply or startup.
        int gStyleHold = 0;

        // Copyright draws before CGame::Process. GTA restyles the HWND to
        // a borderless popup during device setup; without this we would
        // either leave that popup or apply the player's borderless setting
        // over a legal notice.
        bool gCopyrightWindowed = true;

        // Live resize: block CameraSize / SetupBackBufferVertex until RW
        // has Reset, then rebuild. gHoldAge drops the hold if Reset never
        // arrives so a stuck flag cannot freeze the camera forever.
        //
        // Hoodlum's 0x4CC970 is skipped separately and stays skipped
        // through our own CameraSize / SetupBackBufferVertex so those
        // do not walk the packed list.
        bool gHoldRasters         = false;
        bool gSkipHoodlumRestore  = false;
        bool gAwaitingReset       = false;
        bool gResetLanded         = false;
        int gHoldAge              = 0;

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
        LONG WindowedStyle() {
            return (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX) & ~WS_THICKFRAME;
        }

        SIZE ResolveSize(HWND window) {
            const auto monitor = MonitorRectFor(window);
            const LONG screenWidth  = monitor.right - monitor.left;
            const LONG screenHeight = monitor.bottom - monitor.top;

            if (gSettings.borderless && !gSettings.exclusive) {
                return SIZE {screenWidth, screenHeight};
            }

            SIZE size {static_cast<LONG>(gSettings.width), static_cast<LONG>(gSettings.height)};

            if (size.cx <= 0 || size.cy <= 0) {
                size = SIZE {screenWidth, screenHeight};
            }

            // FIT THE OUTER WINDOW, NOT THE CLIENT AREA.
            //
            // Chrome is taken from the style we are about to apply, not
            // GTA's current popup: at CreateDevice the window has no caption
            // yet, so measuring it reports zero chrome and a windowed 0x0
            // (or a mode the size of the monitor) becomes a client that
            // cannot exist. Windows then clips, the device and the window
            // disagree, and the first EndScene SetWindowPos kills the game.
            if (gSettings.exclusive) {
                size.cx = std::min(size.cx, screenWidth);
                size.cy = std::min(size.cy, screenHeight);
                return size;
            }

            RECT frame {0, 0, 0, 0};
            AdjustWindowRect(&frame, static_cast<DWORD>(WindowedStyle()), FALSE);

            const LONG chromeWidth  = frame.right - frame.left;
            const LONG chromeHeight = frame.bottom - frame.top;

            size.cx = std::min(size.cx, std::max(1L, screenWidth - chromeWidth));
            size.cy = std::min(size.cy, std::max(1L, screenHeight - chromeHeight));

            return size;
        }

        // _RwD3D9AdapterInformation (0xC9BCE0) sits 0x360 before Present
        // (0xC9C040). D3DDISPLAYMODE is after name[512] + modeCount.
        void WriteAdapterMode(UINT width, UINT height) {
            const auto present = SDK::gPatterns.D3DPresentParameters;

            if (!present) {
                return;
            }

            auto *const adapter = reinterpret_cast<std::uint8_t *>(present) - 0x360;
            *reinterpret_cast<UINT *>(adapter + 516) = width;
            *reinterpret_cast<UINT *>(adapter + 520) = height;
        }

        void EndLiveRasterHold() {
            gHoldRasters        = false;
            gSkipHoodlumRestore = false;
            gAwaitingReset      = false;
            gResetLanded        = false;
            gHoldAge            = 0;
        }

        // rwVIDEOMODEEXCLUSIVE. Vanilla's dialog only lists exclusive
        // modes; the flag is how we tell that from a windowed leftover.
        constexpr std::int32_t kRwVideoModeExclusive = 1;

        struct RwVideoModeInfo {
            std::int32_t width;
            std::int32_t height;
            std::int32_t depth;
            std::int32_t flags;
            std::int32_t refRate;
            std::int32_t format;
        };

        using GetNumVideoModesFn = std::int32_t(__cdecl *)();
        using GetVideoModeInfoFn = RwVideoModeInfo *(__cdecl *)(RwVideoModeInfo *out, std::int32_t index);
        using DialogFn           = INT_PTR(__stdcall *)(HWND, UINT, WPARAM, LPARAM);

        uintptr_t gSelectShow   = 0;
        uintptr_t gSelectSingle = 0;
        uintptr_t gSelectResume = 0;
        uintptr_t gGcurSelSS    = 0;
        DialogFn gOriginalDialog = nullptr;
        bool gInSetVideoMode     = false;

        std::int32_t ReadInt(uintptr_t address) {
            return *reinterpret_cast<const std::int32_t *>(address);
        }

        void WriteInt(uintptr_t address, std::int32_t value) {
            hook::put<std::int32_t>(address, value);
        }

        bool ReadVideoMode(std::int32_t index, RwVideoModeInfo &out) {
            const auto getInfo = SDK::gPatterns.RwEngineGetVideoModeInfo;

            if (!getInfo) {
                return false;
            }

            return reinterpret_cast<GetVideoModeInfoFn>(getInfo)(&out, index) != nullptr;
        }

        // Closest RW mode for a saved width/height. Exclusive is preferred
        // when the player asked for it: a windowed mode of the same size
        // would make psSelectDevice skip the fullscreen path and leave
        // RsGlobal at the desktop.
        std::int32_t FindVideoMode(std::uint32_t width, std::uint32_t height, bool exclusive) {
            const auto getNum = SDK::gPatterns.RwEngineGetNumVideoModes;

            if (!getNum) {
                return -1;
            }

            const auto count = reinterpret_cast<GetNumVideoModesFn>(getNum)();

            if (count <= 0) {
                return -1;
            }

            std::int32_t fallback = -1;

            for (std::int32_t i = 0; i < count; ++i) {
                RwVideoModeInfo mode {};

                if (!ReadVideoMode(i, mode) || mode.width != static_cast<std::int32_t>(width) || mode.height != static_cast<std::int32_t>(height)) {
                    continue;
                }

                const bool modeExclusive = (mode.flags & kRwVideoModeExclusive) != 0;

                if (exclusive == modeExclusive && mode.depth == 32) {
                    return i;
                }

                if (fallback < 0) {
                    fallback = i;
                }
            }

            return fallback;
        }

        void ApplySavedVideoMode() {
            const auto &config = Core::GetClientConfig();
            const auto &p      = SDK::gPatterns;

            if (gGcurSelSS) {
                WriteInt(gGcurSelSS, static_cast<std::int32_t>(config.displayAdapter));
            }

            if (config.displayWidth == 0 || config.displayHeight == 0 || !p.GcurSelVM) {
                return;
            }

            const auto mode = FindVideoMode(config.displayWidth, config.displayHeight, config.displayExclusive);

            if (mode < 0) {
                return;
            }

            WriteInt(p.GcurSelVM, mode);

            if (p.SavedVideoMode) {
                WriteInt(p.SavedVideoMode, mode);
                WriteInt(p.SavedVideoMode - 4, mode);
            }
        }

        void CaptureDeviceSelection() {
            const auto &p = SDK::gPatterns;

            if (!p.GcurSelVM) {
                return;
            }

            const auto vm = ReadInt(p.GcurSelVM);
            RwVideoModeInfo mode {};

            if (!ReadVideoMode(vm, mode) || mode.width <= 0 || mode.height <= 0) {
                Framework::Logging::GetLogger("Display")->warn("Device selection accepted mode {} but RW has no info for it", vm);
                return;
            }

            // The stock dialog only lists exclusive modes. That is a GPU
            // and a size, not a request for exclusive fullscreen — Vanta
            // stays windowed at what they picked. Exclusive is a Settings
            // checkbox, applied at the next launch.
            const auto adapter = gGcurSelSS ? static_cast<std::uint32_t>(ReadInt(gGcurSelSS)) : 0;
            const auto width   = static_cast<std::uint32_t>(mode.width);
            const auto height  = static_cast<std::uint32_t>(mode.height);

            SetDisplaySettings({width, height, false, false});
            Core::SaveDisplaySettings(width, height, false, false, adapter);

            Framework::Logging::GetLogger("Display")->info(
                "First-setup device selection: adapter {} windowed {}x{}",
                adapter,
                mode.width,
                mode.height);
        }

        void __cdecl DecideSelectDevice() {
            if (Core::IsDisplaySetupDone()) {
                // Do not enumerate RW modes here. psSelectDevice has not
                // called RwEngineSetSubSystem yet; GetNumVideoModes walks a
                // device that is still null (crash at eip 0x3C). Vanta
                // applies the saved size later, in CreateDevice.
                gSelectResume = gSelectSingle;
                Framework::Logging::GetLogger("Display")->info("Device selection skipped; using saved display settings");
                return;
            }

            gSelectResume = gSelectShow;
            Framework::Logging::GetLogger("Display")->info("First setup: showing GTA device selection");
        }

        INT_PTR CALLBACK DeviceSelectDlg(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
            const auto result = gOriginalDialog ? gOriginalDialog(window, msg, wParam, lParam) : FALSE;

            if (msg == WM_INITDIALOG) {
                SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) | WS_EX_APPWINDOW);
                ShowWindow(window, SW_HIDE);
                ShowWindow(window, SW_SHOW);
                return result;
            }

            if (msg == WM_COMMAND && LOWORD(wParam) == IDOK && HIWORD(wParam) == 0) {
                CaptureDeviceSelection();
            }

            return result;
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

        // THE FILE WINS, not GTA's leftover dialog size. Vanta skips the
        // frontend, so BackBufferWidth here is whatever the engine defaulted
        // to - honouring it is why a windowed resolution in VantaMP.json
        // never appeared on screen.
        const auto size = ResolveSize(window);

        const bool incomingDiffered = parameters->BackBufferWidth != static_cast<UINT>(size.cx) || parameters->BackBufferHeight != static_cast<UINT>(size.cy) || parameters->Windowed == (gSettings.exclusive ? TRUE : FALSE);

        gResolved = size;

        parameters->Windowed         = gSettings.exclusive ? FALSE : TRUE;
        parameters->BackBufferWidth  = static_cast<UINT>(size.cx);
        parameters->BackBufferHeight = static_cast<UINT>(size.cy);
        parameters->hDeviceWindow    = window;

        // 0 is required for windowed and is D3DPRESENT_RATE_DEFAULT when
        // exclusive — the adapter picks a rate the mode supports.
        parameters->FullScreen_RefreshRateInHz = 0;

        if (parameters->BackBufferFormat == D3DFMT_UNKNOWN) {
            parameters->BackBufferFormat = D3DFMT_X8R8G8B8;
        }

        if (parameters->EnableAutoDepthStencil && parameters->AutoDepthStencilFormat == D3DFMT_UNKNOWN) {
            parameters->AutoDepthStencilFormat = D3DFMT_D24S8;
        }

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

        // Adapter mode is what the copyright splash and CameraBeginUpdate
        // read. Leave it at GTA's video-mode size and the artwork sits in
        // the top-left of a larger back buffer.
        WriteAdapterMode(static_cast<UINT>(size.cx), static_cast<UINT>(size.cy));

        if (incomingDiffered) {
            Framework::Logging::GetLogger("Display")->info(
                "Device {} at {}x{}",
                gSettings.exclusive ? "exclusive" : "windowed",
                size.cx,
                size.cy);
        }
    }

    void ApplyWindowStyle(HWND window) {
        if (!window || gSettings.exclusive) {
            return;
        }

        const auto monitor = MonitorRectFor(window);

        // The size the device was actually created at, so the window's client
        // area matches the back buffer. Anything else stretches the image or
        // leaves the interface laid out for a size nothing is rendering at.
        const auto size = gResolved.cx > 0 ? gResolved : ResolveSize(window);

        const auto current = GetWindowLongW(window, GWL_STYLE);

        // Chrome only. WS_VISIBLE and the clip bits stay: replacing the
        // whole style with WindowedStyle() cleared VISIBLE and the window
        // vanished, then PulseWindowStyle did that every frame.
        constexpr LONG kChrome = WS_POPUP | WS_BORDER | WS_DLGFRAME | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

        auto desired = current;
        desired &= ~kChrome;

        const bool borderless = gSettings.borderless && !gCopyrightWindowed;

        if (borderless) {
            desired |= WS_POPUP;
        }
        else {
            desired |= WindowedStyle();
        }

        RECT client {};
        GetClientRect(window, &client);

        const bool styleOk = (current & kChrome) == (desired & kChrome);
        const bool sizeOk  = (client.right - client.left) == size.cx && (client.bottom - client.top) == size.cy;

        // Client size, not GetWindowRect. DWM's outer box does not match
        // AdjustWindowRect, so comparing those never settles and SetWindowPos
        // runs every pulse — WM_SIZE, a device Reset, the window gone.
        if (styleOk && sizeOk) {
            return;
        }

        if (!styleOk) {
            SetWindowLongW(window, GWL_STYLE, desired);
        }

        RECT target {0, 0, size.cx, size.cy};
        AdjustWindowRect(&target, static_cast<DWORD>(desired), FALSE);

        const int outerWidth  = target.right - target.left;
        const int outerHeight = target.bottom - target.top;

        const int x = borderless ? monitor.left : monitor.left + ((monitor.right - monitor.left) - outerWidth) / 2;
        const int y = borderless ? monitor.top : monitor.top + ((monitor.bottom - monitor.top) - outerHeight) / 2;

        UINT flags = SWP_NOZORDER | SWP_SHOWWINDOW;

        if (!styleOk) {
            flags |= SWP_FRAMECHANGED;
        }

        SetWindowPos(window, nullptr, x, y, outerWidth, outerHeight, flags);

        Framework::Logging::GetLogger("Display")->info("Window {} at {}x{}, origin {},{}", borderless ? "borderless" : "windowed", size.cx, size.cy, x, y);
    }

    void HoldWindowStyle(int frames) {
        gStyleHold = frames;
    }

    void ApplySavedWindowStyle() {
        gCopyrightWindowed = false;
        HoldWindowStyle(180);
    }

    void PulseWindowStyle(HWND window) {
        if (!gCopyrightWindowed && gStyleHold <= 0) {
            return;
        }

        if (gStyleHold > 0) {
            --gStyleHold;
        }

        ApplyWindowStyle(window);
    }

    void RebuildCameraAndPostFx() {
        RefreshGameView();

        // SetupBackBufferVertex only recreates when the 2^n size changes.
        // 1280 and 1600 both become 2048, so the pointer stays and post-FX
        // draws through the surface Reset freed.
        if (const auto slot = SDK::gPatterns.CPostEffects__pRasterFrontBuffer) {
            auto *&raster = *reinterpret_cast<void **>(slot);

            if (raster) {
                if (const auto destroy = SDK::gPatterns.RwRasterDestroy) {
                    reinterpret_cast<int(__cdecl *)(void *)>(destroy)(raster);
                }

                raster = nullptr;
            }
        }

        if (const auto setup = SDK::gPatterns.CPostEffects__SetupBackBufferVertex) {
            reinterpret_cast<void(__cdecl *)()>(setup)();
        }
    }

    bool IsGameChangingVideoMode() {
        return gInSetVideoMode;
    }

    bool ApplyLiveDisplay(IDirect3DDevice9 *, HWND) {
        // Not used. SetVideoMode forces exclusive fullscreen and then
        // Hoodlum's size-changing Reset dies in d3d9.dll+0x8D06F.
        return false;
    }

    void HoldRastersForLiveApply() {
        gHoldRasters        = true;
        gSkipHoodlumRestore = true;
        gAwaitingReset      = true;
        gResetLanded        = false;
        gHoldAge            = 0;
    }

    bool IsRasterHoldActive() {
        return gHoldRasters;
    }

    bool ShouldSkipHoodlumRasterRestore() {
        return gSkipHoodlumRestore;
    }

    void NoteLiveDeviceReset() {
        if (gAwaitingReset) {
            gResetLanded = true;
        }
    }

    void PulseRasterHold() {
        if (!gAwaitingReset || gResetLanded) {
            return;
        }

        if (++gHoldAge > 30) {
            gHoldRasters        = false;
            gSkipHoodlumRestore = false;
            gAwaitingReset      = false;
            Framework::Logging::GetLogger("Display")->error("Live display apply timed out waiting for RW to reset");
        }
    }

    bool FinishLiveDisplayResetIfNeeded() {
        if (!gAwaitingReset || !gResetLanded) {
            return false;
        }

        gAwaitingReset = false;
        gResetLanded   = false;
        gHoldRasters   = false;
        gHoldAge       = 0;
        // gSkipHoodlumRestore stays set until the rebuild below finishes.

        RebuildCameraAndPostFx();
        gSkipHoodlumRestore = false;

        Framework::Logging::GetLogger("Display")->info("Camera and post-FX rasters rebuilt after RW reset");
        return true;
    }

    void RefreshGameView() {
        SIZE size = gResolved;

        if (size.cx <= 0 || size.cy <= 0) {
            if (const auto rsGlobal = SDK::gPatterns.RsGlobal; rsGlobal) {
                size.cx = *reinterpret_cast<const std::int32_t *>(rsGlobal + 4);
                size.cy = *reinterpret_cast<const std::int32_t *>(rsGlobal + 8);
            }
        }

        if (size.cx <= 0 || size.cy <= 0) {
            return;
        }

        void *rwCamera = nullptr;

        if (const auto rsGlobal = SDK::gPatterns.RsGlobal; rsGlobal) {
            rwCamera = *reinterpret_cast<void **>(rsGlobal - 4);
        }

        if (!rwCamera) {
            if (const auto theCamera = SDK::gPatterns.TheCamera; theCamera) {
                rwCamera = *reinterpret_cast<void **>(theCamera + 0x954);
            }
        }

        if (!rwCamera) {
            return;
        }

        // CameraSize (0x72FC70) ignores a passed rect when the current RW
        // mode is exclusive: it overwrites width/height with that mode.
        // First-setup picks an exclusive mode, CreateDevice is windowed at
        // our size, and the splash / world then draw in the top-left.
        // Write the rasters ourselves so they match the back buffer.
        constexpr std::size_t kFrameBufferOffset = 0x60;
        constexpr std::size_t kZBufferOffset     = 0x64;
        constexpr std::size_t kRasterWidth       = 0x0C;
        constexpr std::size_t kRasterHeight      = 0x10;
        constexpr std::size_t kViewWindow        = 0x68;

        auto *const cameraBytes = static_cast<std::uint8_t *>(rwCamera);
        void *const raster      = *reinterpret_cast<void **>(cameraBytes + kFrameBufferOffset);
        void *const zRaster     = *reinterpret_cast<void **>(cameraBytes + kZBufferOffset);

        if (!raster) {
            return;
        }

        *reinterpret_cast<std::int32_t *>(static_cast<std::uint8_t *>(raster) + kRasterWidth)  = size.cx;
        *reinterpret_cast<std::int32_t *>(static_cast<std::uint8_t *>(raster) + kRasterHeight) = size.cy;

        if (zRaster) {
            *reinterpret_cast<std::int32_t *>(static_cast<std::uint8_t *>(zRaster) + kRasterWidth)  = size.cx;
            *reinterpret_cast<std::int32_t *>(static_cast<std::uint8_t *>(zRaster) + kRasterHeight) = size.cy;
        }

        if (const auto rsGlobal = SDK::gPatterns.RsGlobal; rsGlobal) {
            hook::put<std::int32_t>(rsGlobal + 4, static_cast<std::int32_t>(size.cx));
            hook::put<std::int32_t>(rsGlobal + 8, static_cast<std::int32_t>(size.cy));
        }

        WriteAdapterMode(static_cast<UINT>(size.cx), static_cast<UINT>(size.cy));

        const float view = 0.7f;
        const float aspect = static_cast<float>(size.cx) / static_cast<float>(size.cy);
        *reinterpret_cast<float *>(cameraBytes + kViewWindow)     = view;
        *reinterpret_cast<float *>(cameraBytes + kViewWindow + 4) = view / aspect;
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

    void InstallDeviceSelectionHook() {
        const auto &p = SDK::gPatterns;

        if (!p.PsSelectDeviceHook || !p.VidModeDialogFuncPush) {
            Framework::Logging::GetLogger("Display")->error("Device selection sites did not resolve; GTA's dialog is left alone");
            return;
        }

        const auto site = p.PsSelectDeviceHook;
        const auto rel  = *reinterpret_cast<const std::int8_t *>(site + 13);

        gSelectShow   = site + 14;
        gSelectSingle = site + 14 + rel;
        gGcurSelSS    = *reinterpret_cast<const std::uint32_t *>(site + 1) + 4;
        gOriginalDialog = *reinterpret_cast<DialogFn *>(p.VidModeDialogFuncPush + 1);

        hook::put<DialogFn>(p.VidModeDialogFuncPush + 1, &DeviceSelectDlg);

        // Mid-function hook the way MTA does: steal the `mov eax,
        // [GnumSubSystems]` at 0x746219 and resume at either the show
        // path or the single-adapter path. A compiler-generated function
        // cannot do that, so the trampoline is written by hand.
        auto *const stub = static_cast<std::uint8_t *>(VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));

        if (!stub) {
            Framework::Logging::GetLogger("Display")->error("Device selection trampoline could not be allocated");
            return;
        }

        stub[0] = 0x60; // pushad
        stub[1] = 0xE8;

        const auto callRel = reinterpret_cast<std::intptr_t>(&DecideSelectDevice) - (reinterpret_cast<std::intptr_t>(stub) + 6);
        std::memcpy(stub + 2, &callRel, sizeof(std::uint32_t));

        stub[6]  = 0x61; // popad
        stub[7]  = 0xBF; // mov edi, 1 — MultipleSubSystems is written from edi
        stub[8]  = 0x01;
        stub[9]  = 0x00;
        stub[10] = 0x00;
        stub[11] = 0x00;
        stub[12] = 0xFF; // jmp dword ptr [gSelectResume]
        stub[13] = 0x25;

        const auto resumePtr = reinterpret_cast<std::uint32_t>(&gSelectResume);
        std::memcpy(stub + 14, &resumePtr, sizeof(resumePtr));

        hook::jump(site, stub);

        Framework::Logging::GetLogger("Display")->info(
            Core::IsDisplaySetupDone() ? "Device selection will be skipped this launch" : "Device selection will run once for first setup");
    }
} // namespace VantaMP::Game
