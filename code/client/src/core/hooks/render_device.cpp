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

namespace {
    constexpr size_t kCreateDeviceSlot = 16;
    constexpr size_t kResetSlot        = 16;
    constexpr size_t kEndSceneSlot     = 42;

    using Direct3DCreate9Proc = IDirect3D9 *(WINAPI *)(UINT);
    using CreateDeviceProc    = HRESULT(STDMETHODCALLTYPE *)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
    using ResetProc           = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
    using EndSceneProc        = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *);

    Direct3DCreate9Proc gDirect3DCreate9 = nullptr;
    CreateDeviceProc gCreateDevice       = nullptr;
    ResetProc gReset                     = nullptr;
    EndSceneProc gEndScene               = nullptr;

    bool gDeviceHooksInstalled = false;
    bool gCreateDeviceHooked   = false;

    void *VTableEntry(void *object, size_t slot) {
        return (*reinterpret_cast<void ***>(object))[slot];
    }

    HWND gGameWindow = nullptr;

    // The only point in the frame that is both inside a scene and after the world updated.
    HRESULT STDMETHODCALLTYPE EndScene_Hook(IDirect3DDevice9 *device) {
        // The game sets its own window style during device setup, after
        // CreateDevice has returned, so styling it there alone is undone before
        // anyone sees it.
        //
        // RE-ASSERTED FOR A WHILE, NOT FOR EVER. ApplyWindowStyle calls
        // SetWindowPos, which sends WM_SIZE to the window procedure - and the
        // interface listens to that. Left running every frame the two feed each
        // other: style the window, resize the browser, style it again. A few
        // hundred frames is long past the game's own settings load and well
        // short of anything the player can see.
        static int framesLeft = 300;

        if (framesLeft > 0) {
            --framesLeft;
            VantaMP::Game::ApplyWindowStyle(gGameWindow);
        }

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
        return gEndScene(device);
    }

    HRESULT STDMETHODCALLTYPE Reset_Hook(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *presentationParameters) {
        VantaMP::Core::OnDeviceLost();
        const auto result = gReset(device, presentationParameters);
        VantaMP::Core::OnDeviceReset();
        return result;
    }

    void InstallDeviceHooks(IDirect3DDevice9 *device, HWND window) {
        if (gDeviceHooksInstalled) {
            return;
        }
        gDeviceHooksInstalled = true;
        gGameWindow           = window;

        MH_CreateHook(VTableEntry(device, kEndSceneSlot), reinterpret_cast<LPVOID>(&EndScene_Hook), reinterpret_cast<void **>(&gEndScene));
        MH_CreateHook(VTableEntry(device, kResetSlot), reinterpret_cast<LPVOID>(&Reset_Hook), reinterpret_cast<void **>(&gReset));
        MH_EnableHook(MH_ALL_HOOKS);

        Framework::Logging::GetLogger("RenderDevice")->info("Hooked the game's D3D9 device {} on window {}", static_cast<void *>(device), static_cast<void *>(window));

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
    },
    "RenderDevice");
