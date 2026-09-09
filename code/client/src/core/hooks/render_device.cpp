#include <utils/safe_win32.h>

#include <MinHook.h>
#include <d3d9.h>

#include <logging/logger.h>
#include <utils/hooking/hook_function.h>

#include "core/application.h"
#include "core/nui/nui.h"
#include "core/ui/window_input.h"
#include "game/patches/display.h"
#include "game/patches/input.h"
#include "sdk/patterns.h"

namespace {
    constexpr size_t kCreateDeviceSlot = 16;
    constexpr size_t kResetSlot        = 16;
    constexpr size_t kPresentSlot      = 17;
    constexpr size_t kEndSceneSlot     = 42;

    using Direct3DCreate9Proc = IDirect3D9 *(WINAPI *)(UINT);
    using CreateDeviceProc    = HRESULT(STDMETHODCALLTYPE *)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
    using ResetProc           = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
    using PresentProc         = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
    using CGameProcessProc    = void(__cdecl *)();
    using EndSceneProc        = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *);
    using CameraSizeProc      = void(__cdecl *)(void *, void *, float, float);
    using SetupBackBufferProc = void(__cdecl *)();
    using RwCameraClearProc   = void *(__cdecl *)(void *, void *, int);
    using RasterRestoreProc   = int(__cdecl *)();

    Direct3DCreate9Proc gDirect3DCreate9 = nullptr;
    CreateDeviceProc gCreateDevice       = nullptr;
    ResetProc gReset                     = nullptr;
    PresentProc gPresent                 = nullptr;
    CGameProcessProc gCGameProcess       = nullptr;
    EndSceneProc gEndScene               = nullptr;
    CameraSizeProc gCameraSize           = nullptr;
    SetupBackBufferProc gSetupBackBuffer = nullptr;
    RwCameraClearProc gRwCameraClear     = nullptr;
    RasterRestoreProc gRasterListRestore = nullptr;

    bool gDeviceHooksInstalled = false;
    bool gCreateDeviceHooked   = false;

    void *VTableEntry(void *object, size_t slot) {
        return (*reinterpret_cast<void ***>(object))[slot];
    }

    HWND gGameWindow               = nullptr;
    IDirect3DDevice9 *gDevice      = nullptr;

    // The only point in the frame that is both inside a scene and after the world updated.
    HRESULT STDMETHODCALLTYPE EndScene_Hook(IDirect3DDevice9 *device) {
        // The game sets its own window style during device setup, after
        // CreateDevice has returned, so styling it there alone is undone before
        // anyone sees it.
        //
        // Keeps the pointer inside the window, and lets it go on alt-tab.
        VantaMP::UI::UpdateCursorClip(gGameWindow);

        // Keeps GTA's foreground flag honest, and stops the game acting on
        // input it should not have.
        //
        // WITHOUT THE SUPPRESSION, ONE KEYPRESS MOVES EVERY RUNNING CLIENT.
        // DirectInput hands a process the keyboard whether or not its window is
        // focused, and the foreground flag only gates the mouse - so an
        // unfocused second client reads the same press as genuine input and
        // sends it upstream as its own player's.
        //
        // The interface is the other half: typing into it would otherwise drive
        // the player at the same time.
        const bool focused = VantaMP::Game::HasWindowFocus(gGameWindow);

        VantaMP::Game::UpdateForegroundFocus(gGameWindow);
        VantaMP::Game::SuppressGameInput(!focused || VantaMP::Nui::IsCapturingInput());

        VantaMP::Core::TickApplication();
        VantaMP::Game::PulseWindowStyle(gGameWindow);
        return gEndScene(device);
    }

    void __cdecl CameraSize_Hook(void *camera, void *rect, float viewWindow, float aspectRatio) {
        if (VantaMP::Game::IsRasterHoldActive()) {
            return;
        }

        gCameraSize(camera, rect, viewWindow, aspectRatio);
        VantaMP::Game::RefreshGameView();
    }

    void __cdecl SetupBackBufferVertex_Hook() {
        if (VantaMP::Game::IsRasterHoldActive()) {
            return;
        }

        gSetupBackBuffer();
    }

    int __cdecl RasterListRestore_Hook() {
        // Hoodlum 1.0: 0x4CC970 is a stub that jumps into unpacked code at
        // 0x674AD06F. That path is fine for a same-size device-lost Reset
        // and dies on a live back-buffer size change. Return success and
        // let FinishLiveDisplayResetIfNeeded recreate camera / post-FX.
        if (VantaMP::Game::ShouldSkipHoodlumRasterRestore()) {
            Framework::Logging::GetLogger("Display")->info("Skipped Hoodlum raster-list restore during live resize");
            return 1;
        }

        return gRasterListRestore();
    }

    void *__cdecl RwCameraClear_Hook(void *camera, void *color, int flags) {
        // CameraBeginUpdate has already Reset and restored. Rebuild now,
        // before post-FX samples the freed 2048x1024 front buffer.
        if (VantaMP::Game::FinishLiveDisplayResetIfNeeded()) {
            RECT client {};
            GetClientRect(gGameWindow, &client);
            VantaMP::Nui::ResizeInterface(client.right - client.left, client.bottom - client.top);
            VantaMP::Nui::RefreshSettings();
        }

        return gRwCameraClear(camera, color, flags);
    }

    void __cdecl CGameProcess_Hook() {
        // Outside every D3D method. Reset from BeginScene or Present is
        // INVALIDCALL; the 02:21 log is that, not a stale DLL.
        VantaMP::Core::FlushDisplayApply(gDevice, gGameWindow);
        VantaMP::Game::PulseWindowStyle(gGameWindow);
        VantaMP::Game::PulseRasterHold();
        gCGameProcess();
    }

    HRESULT STDMETHODCALLTYPE Present_Hook(IDirect3DDevice9 *device, const RECT *source, const RECT *dest, HWND overrideWindow, const RGNDATA *dirty) {
        const auto result = gPresent(device, source, dest, overrideWindow, dirty);
        VantaMP::UI::UpdateCursorClip(gGameWindow);
        return result;
    }

    HRESULT STDMETHODCALLTYPE Reset_Hook(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *presentationParameters) {
        // SetVideoMode already filled these. Rewriting them here is what
        // made a homemade Reset disagree with RW and crash.
        if (!VantaMP::Game::IsGameChangingVideoMode()) {
            VantaMP::Game::ApplyToPresentParameters(presentationParameters, gGameWindow);
        }
        VantaMP::Core::OnDeviceLost();
        const auto result = gReset(device, presentationParameters);

        if (SUCCEEDED(result)) {
            VantaMP::Game::NoteLiveDeviceReset();
            VantaMP::Game::RefreshGameView();
            Framework::Logging::GetLogger("RenderDevice")->info(
                "Device reset to {}x{}",
                presentationParameters ? presentationParameters->BackBufferWidth : 0,
                presentationParameters ? presentationParameters->BackBufferHeight : 0);
        }

        // INVALIDCALL (Reset refused, still in a scene) leaves the old
        // device up — ImGui has to come back. DEVICELOST does not.
        if (SUCCEEDED(result) || result == D3DERR_INVALIDCALL) {
            VantaMP::Core::OnDeviceReset();
        }

        return result;
    }

    void InstallDeviceHooks(IDirect3DDevice9 *device, HWND window) {
        if (gDeviceHooksInstalled) {
            return;
        }
        gDeviceHooksInstalled = true;
        gGameWindow           = window;
        gDevice               = device;

        MH_CreateHook(VTableEntry(device, kEndSceneSlot), reinterpret_cast<LPVOID>(&EndScene_Hook), reinterpret_cast<void **>(&gEndScene));
        MH_CreateHook(VTableEntry(device, kPresentSlot), reinterpret_cast<LPVOID>(&Present_Hook), reinterpret_cast<void **>(&gPresent));
        MH_CreateHook(VTableEntry(device, kResetSlot), reinterpret_cast<LPVOID>(&Reset_Hook), reinterpret_cast<void **>(&gReset));
        MH_EnableHook(MH_ALL_HOOKS);

        VantaMP::Core::BringUpApplication(device, window);
    }

    HRESULT STDMETHODCALLTYPE CreateDevice_Hook(IDirect3D9 *d3d, UINT adapter, D3DDEVTYPE deviceType, HWND focusWindow, DWORD behaviourFlags, D3DPRESENT_PARAMETERS *presentationParameters, IDirect3DDevice9 **returnedDevice) {
        const auto window = presentationParameters && presentationParameters->hDeviceWindow ? presentationParameters->hDeviceWindow : focusWindow;

        // BEFORE the device is made, because presentation parameters are what
        // it is made FROM - changing them afterwards means destroying and
        // recreating the device, and GTA holds references to the old one.
        VantaMP::Game::ApplyToPresentParameters(presentationParameters, window);

        const auto result = gCreateDevice(d3d, adapter, deviceType, focusWindow, behaviourFlags, presentationParameters, returnedDevice);
        if (SUCCEEDED(result) && returnedDevice && *returnedDevice) {
            // After, so the window is styled against a device that exists.
            VantaMP::Game::ApplyWindowStyle(window);

            // And now the window's shape is known, so the projection can be
            // corrected for it. Has to follow ApplyWidescreenPatches, which
            // stops the game recomputing this from its own 4:3 assumption
            // every frame.
            VantaMP::Game::ApplyAspectRatio(window);
            VantaMP::Game::RefreshGameView();
            VantaMP::Game::HoldWindowStyle(300);
            InstallDeviceHooks(*returnedDevice, window);
        }
        return result;
    }

    // Earliest point the device's vtable can be reached: hook the factory that makes it.
    IDirect3D9 *WINAPI Direct3DCreate9_Hook(UINT sdkVersion) {
        const auto d3d = gDirect3DCreate9(sdkVersion);
        if (!d3d || gCreateDeviceHooked) {
            return d3d;
        }

        gCreateDeviceHooked = true;
        MH_CreateHook(VTableEntry(d3d, kCreateDeviceSlot), reinterpret_cast<LPVOID>(&CreateDevice_Hook), reinterpret_cast<void **>(&gCreateDevice));
        MH_EnableHook(MH_ALL_HOOKS);
        return d3d;
    }
} // namespace

static InitFunction init(
    []() {
        const auto logger = Framework::Logging::GetLogger("RenderDevice");

        // We run from the game's CRT startup, before it touches Direct3D
        const auto d3d9 = LoadLibraryW(L"d3d9.dll");
        if (!d3d9) {
            logger->error("d3d9.dll could not be loaded, the client has no renderer to attach to");
            return;
        }

        const auto create = GetProcAddress(d3d9, "Direct3DCreate9");
        if (!create) {
            logger->error("d3d9.dll has no Direct3DCreate9 export");
            return;
        }

        MH_CreateHook(reinterpret_cast<LPVOID>(create), reinterpret_cast<LPVOID>(&Direct3DCreate9_Hook), reinterpret_cast<void **>(&gDirect3DCreate9));

        if (const auto process = VantaMP::SDK::gPatterns.CGame__Process) {
            MH_CreateHook(reinterpret_cast<LPVOID>(process), reinterpret_cast<LPVOID>(&CGameProcess_Hook), reinterpret_cast<void **>(&gCGameProcess));
        }

        if (const auto cameraSize = VantaMP::SDK::gPatterns.CameraSize) {
            MH_CreateHook(reinterpret_cast<LPVOID>(cameraSize), reinterpret_cast<LPVOID>(&CameraSize_Hook), reinterpret_cast<void **>(&gCameraSize));
        }

        if (const auto setup = VantaMP::SDK::gPatterns.CPostEffects__SetupBackBufferVertex) {
            MH_CreateHook(reinterpret_cast<LPVOID>(setup), reinterpret_cast<LPVOID>(&SetupBackBufferVertex_Hook), reinterpret_cast<void **>(&gSetupBackBuffer));
        }

        if (const auto clear = VantaMP::SDK::gPatterns.RwCameraClear) {
            MH_CreateHook(reinterpret_cast<LPVOID>(clear), reinterpret_cast<LPVOID>(&RwCameraClear_Hook), reinterpret_cast<void **>(&gRwCameraClear));
        }

        if (const auto restore = VantaMP::SDK::gPatterns.RwD3D9RestoreRasterList) {
            MH_CreateHook(reinterpret_cast<LPVOID>(restore), reinterpret_cast<LPVOID>(&RasterListRestore_Hook), reinterpret_cast<void **>(&gRasterListRestore));
        }
    },
    "RenderDevice");
