#include "window_input.h"

#include "core/nui/nui.h"

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <gui/manager.h>
#include <logging/logger.h>

namespace VantaMP::UI {
    namespace {
        WNDPROC gOriginalProc = nullptr;
        HWND gWindow          = nullptr;

        bool IsMouseMessage(UINT message) {
            return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_MOUSELEAVE;
        }

        bool IsKeyboardMessage(UINT message) {
            return (message >= WM_KEYFIRST && message <= WM_KEYLAST);
        }

        bool IsRepeat(LPARAM lParam) {
            return (lParam & (1 << 30)) != 0;
        }

        // CEF only receives keys while the view has focus. Closing the menu
        // unfocuses it, so F1 / Esc / F8 / ~ have to be taken here.
        bool HandleHotkey(WPARAM wParam) {
            switch (wParam) {
            case VK_F1:
                VantaMP::Nui::ToggleMenu();
                return true;
            case VK_ESCAPE:
                VantaMP::Nui::HandleEscape();
                return true;
            case VK_F8:
            case VK_OEM_3:
            case VK_OEM_8:
                VantaMP::Nui::ToggleConsole();
                return true;
            default:
                return false;
            }
        }

        LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
            if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !IsRepeat(lParam) && HandleHotkey(wParam)) {
                return 0;
            }

            if (auto *manager = Framework::CoreModules::GetWebManager(); manager && manager->IsInitialized()) {
                if (IsMouseMessage(message)) {
                    manager->ProcessMouseEvent(window, message, wParam, lParam);
                }
                else if (IsKeyboardMessage(message)) {
                    manager->ProcessKeyboardEvent(window, message, wParam, lParam);
                }
                else if (message == WM_SIZE) {
                    // ONLY ON A REAL CHANGE.
                    //
                    // Resizing CEF makes it rebuild its texture, and this
                    // message arrives from our own SetWindowPos as well as from
                    // the user - so acting on every one is a loop: style the
                    // window, get WM_SIZE, rebuild the browser surface, style
                    // the window again. That killed the client the moment the
                    // interface existed.
                    static int lastWidth  = 0;
                    static int lastHeight = 0;

                    const int width  = LOWORD(lParam);
                    const int height = HIWORD(lParam);

                    if (width != lastWidth || height != lastHeight) {
                        lastWidth  = width;
                        lastHeight = height;

                        manager->Resize(width, height);
                    }
                }
            }

            return CallWindowProcW(gOriginalProc, window, message, wParam, lParam);
        }
    } // namespace

    void InstallWindowHook(HWND window) {
        if (gOriginalProc || !window) {
            return;
        }

        gWindow       = window;
        gOriginalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));

        if (!gOriginalProc) {
            Framework::Logging::GetLogger("NUI")->error("The window procedure could not be subclassed; the interface will not take input");
            return;
        }

        Framework::Logging::GetLogger("NUI")->info("Window input is being forwarded to the interface");
    }

    void UpdateCursorClip(HWND window) {
        static bool clipped = false;

        if (!window) {
            return;
        }

        const bool focused = GetForegroundWindow() == window;

        if (focused == clipped) {
            return;
        }

        clipped = focused;

        if (!focused) {
            ClipCursor(nullptr);
            return;
        }

        // The client area in screen coordinates. GetClientRect gives it
        // relative to the window, which ClipCursor does not want.
        RECT client {};
        GetClientRect(window, &client);

        POINT topLeft {client.left, client.top};
        POINT bottomRight {client.right, client.bottom};

        ClientToScreen(window, &topLeft);
        ClientToScreen(window, &bottomRight);

        const RECT screen {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
        ClipCursor(&screen);
    }

    void RemoveWindowHook() {
        if (!gOriginalProc || !gWindow) {
            return;
        }

        // Let the pointer go with the hook, or it stays confined to a window
        // nothing is forwarding input to any more.
        ClipCursor(nullptr);

        SetWindowLongPtrW(gWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gOriginalProc));

        gOriginalProc = nullptr;
        gWindow       = nullptr;
    }
} // namespace VantaMP::UI
