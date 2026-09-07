#pragma once

#include <cstdint>

struct HWND__;
using HWND = HWND__ *;
struct IDirect3DDevice9;
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

        // Exclusive fullscreen. Takes the display mode; applied at CreateDevice.
        bool exclusive = false;
    };

    void SetDisplaySettings(const DisplaySettings &settings);
    [[nodiscard]] const DisplaySettings &GetDisplaySettings();

    // Unused. A live size change on this exe crashes, whether we call
    // SetVideoMode or Reset ourselves. Settings write the file and ask
    // for a restart.
    [[nodiscard]] bool ApplyLiveDisplay(IDirect3DDevice9 *device, HWND window);

    // True while SetVideoMode is on the stack. Reset then belongs to
    // the game; rewriting D3DPRESENT_PARAMETERS would undo it.
    [[nodiscard]] bool IsGameChangingVideoMode();

    // WM_SIZE / WM_SIZING would rebuild camera and post-FX rasters on the
    // still-old device. Hoodlum's raster-list restore then dies at
    // 0x674AD06F. Hold those rebuilds until after RW Reset.
    void HoldRastersForLiveApply();
    [[nodiscard]] bool IsRasterHoldActive();
    [[nodiscard]] bool ShouldSkipHoodlumRasterRestore();
    void NoteLiveDeviceReset();
    void PulseRasterHold();

    // After RW restore: CameraSize to the new RsGlobal, then force a new
    // pRasterFrontBuffer. SetupBackBufferVertex rounds to 2^n, so 1280
    // and 1600 share 2048x1024 and it will not recreate on its own.
    [[nodiscard]] bool FinishLiveDisplayResetIfNeeded();

    // Rebuilds the game camera rasters after a live resize. CameraSize
    // (plugin-sdk / 0x72FC70) is what DoRWStuffStartOfFrame eventually
    // calls; doing it here means the next present is already at the new
    // size instead of flashing the old (now destroyed) surfaces.
    // Not safe on the game's first Reset — the camera has no raster yet.
    void RefreshGameView();

    // GTA restyles its window after a Reset. Hold the player's chrome
    // for a few hundred frames, then stop so SetWindowPos and WM_SIZE
    // do not feed each other.
    void HoldWindowStyle(int frames);
    void PulseWindowStyle(HWND window);

    // Rewrites the parameters GTA is about to create its device with.
    //
    // Size comes from VantaMP.json, not GTA's leftover dialog. Windowed
    // unless the player asked for exclusive fullscreen. Exclusive takes
    // the display mode and costs a Reset on every alt-tab; borderless
    // covering the monitor is the default because it looks the same and
    // does not.
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

    // After the copyright screen. Until this runs the HWND stays a
    // normal windowed frame, even if the player asked for borderless.
    void ApplySavedWindowStyle();

    // GTA's Device Selection dialog, first setup only.
    //
    // Vanilla shows it only when there is more than one GPU. MTA hooks
    // the same compare and then persists a "device selection enabled"
    // flag. We show it until VantaMP.json says the player has confirmed
    // a display, then never again — the file is what CreateDevice reads.
    void InstallDeviceSelectionHook();

    // Unlocks the game's widescreen modes and projects at the real aspect ratio.
    //
    // GTA gates its widescreen resolutions behind a check, and then projects
    // everything for 4:3 regardless - so a 16:9 window shows a stretched image
    // with the HUD inset from the corners rather than at them.
    //
    // Applied at startup, before the game has computed anything from its own
    // assumption.
    void ApplyWidescreenPatches();

    // Recomputes the projection and HUD layout for the window's actual shape.
    //
    // Called after the size is known. Separate from the patches above because
    // the ratio depends on the window, which does not exist when they are
    // applied.
    void ApplyAspectRatio(HWND window);

    // Caps the frame rate, or removes the cap when `fps` is 0.
    //
    // The game ships with its limiter OFF and the frontend option that turns it
    // on is one Vanta's startup skips, so an unpatched client renders as fast
    // as the hardware allows. On a modern machine that is several hundred
    // frames a second of a 2004 game - which heats the GPU for nothing and,
    // worse, drives GTA's frame-time-dependent physics into territory it was
    // never tested in.
    //
    // RE-APPLIED, NOT SET ONCE. The game writes this field itself when it loads
    // settings and when the frontend is touched, so a value set at startup does
    // not necessarily survive.
    void SetFrameLimit(int fps);
} // namespace VantaMP::Game
