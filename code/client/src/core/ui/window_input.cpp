#include "window_input.h"

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

        LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
            if (auto *manager = Framework::CoreModules::GetWebManager(); manager && manager->IsInitialized()) {
                if (IsMouseMessage(message)) {
                    manager->ProcessMouseEvent(window, message, wParam, lParam);
                }
                else if (IsKeyboardMessage(message)) {
                    manager->ProcessKeyboardEvent(window, message, wParam, lParam);
                }
                else if (message == WM_SIZE) {
                    // The view paints at a fixed size; a resized window would
                    // otherwise stretch it.
                    manager->Resize(LOWORD(lParam), HIWORD(lParam));
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

    void RemoveWindowHook() {
        if (!gOriginalProc || !gWindow) {
            return;
        }

        SetWindowLongPtrW(gWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gOriginalProc));

        gOriginalProc = nullptr;
        gWindow       = nullptr;
    }
} // namespace VantaMP::UI
