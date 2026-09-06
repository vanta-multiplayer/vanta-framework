#include "game/patches/input.h"

#include "sdk/patterns.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

#include "sdk/game.h"

namespace VantaMP::Game {
    namespace {
        using UpdatePadsFn = void (*)();

        UpdatePadsFn gOriginalUpdatePads = nullptr;
        bool         gSuppressingInput   = false;

        void UpdatePads_Hook() {
            gOriginalUpdatePads();

            // AFTER THE FILL. DirectInput has just written this frame's
            // keys; ProcessControl has not read them yet. Clearing here
            // is what actually stops WASD while Esc is up.
            if (gSuppressingInput) {
                SDK::ClearLocalInput(false);
            }
        }

        // What the exclusive-mode decision is overwritten with:
        //
        //   B0 00   mov al, 0    ; "not exclusive"
        //   90 90   nop nop
        //
        // Written as one dword, little-endian, hence the reversed look.
        constexpr std::uint32_t kNonExclusive = 0x909000B0u;

        // Two PUSH/CALL pairs - ShowCursor(0) and SetCursor(0) - and nothing
        // else, so filling them with NOPs removes the behaviour cleanly.
        constexpr std::size_t kCursorHidingSize = 16u;
    } // namespace

    void ReleaseMouseCapture() {
        const auto &p = SDK::gPatterns;

        // Non-exclusive acquisition: the game still reads deltas, but the
        // pointer stops being its property.
        hook::put<std::uint32_t>(p.DirectInput__MouseExclusiveMode, kNonExclusive);

        // AND STOP IT REACQUIRING. Without these the game re-initialises the
        // device - on alt-tab, on losing focus - and takes the cursor back in
        // exclusive mode, which is why the problem used to return the moment
        // you switched windows.
        hook::put<std::uint8_t>(p.DirectInput__MouseReinit1, 0xEB);
        hook::put<std::uint8_t>(p.DirectInput__MouseReinit2, 0xEB);
        hook::put<std::uint8_t>(p.DirectInput__MouseReinit3, 0xEB);

        // Non-exclusive stops the game OWNING the cursor; this routine still
        // calls SetCursorPos to snap it back to the window centre every frame.
        //
        // Safe to neutralise: __cdecl with a caller-cleaned argument, so an
        // early RET leaves the stack balanced.
        hook::return_function(p.Cursor__Recentre);

        // And stop it hiding the cursor. The client shows a menu before it
        // shows a world, and the game would otherwise undo every attempt to
        // make a pointer visible.
        hook::nop(p.Cursor__Hiding, kCursorHidingSize);

        Framework::Logging::GetLogger("Input")->info("Mouse released: non-exclusive, no re-centring, no cursor hiding");
    }

    void InstallPadSuppressHook() {
        if (gOriginalUpdatePads) {
            return;
        }

        const auto site = SDK::gPatterns.CPad__UpdatePadsCall;

        gOriginalUpdatePads = reinterpret_cast<UpdatePadsFn>(hook::get_call(site));
        hook::call(site, &UpdatePads_Hook);

        Framework::Logging::GetLogger("Input")->info("Pad suppress hooked after UpdatePads");
    }

    void MarkGameForeground(bool foreground) {
        // A plain global rather than code, so no protection change is needed.
        *reinterpret_cast<std::int32_t *>(SDK::gPatterns.IsForegroundApp) = foreground ? 1 : 0;
    }

    bool HasWindowFocus(HWND window) {
        if (!window) {
            // Before the window exists there is nothing to be unfocused from,
            // and answering "no" here would suppress input during startup.
            return true;
        }

        return GetForegroundWindow() == window;
    }

    void UpdateForegroundFocus(HWND window) {
        MarkGameForeground(HasWindowFocus(window));
    }

    void SuppressGameInput(bool suppress) {
        if (suppress) {
            // CLEARED EVERY FRAME, NOT ONCE. The game repopulates the pad from
            // DirectInput on each update, so a single clear is overwritten
            // immediately.
            //
            // Clear() rather than zeroing the struct: the game keeps button
            // history and timing alongside the current state, and a memset
            // would leave those inconsistent.
            // false: player controls stay disabled while suppressed.
            // true: the phase is reset, so a key held when the menu opened is
            // not treated as newly pressed when it closes.
            SDK::ClearLocalInput(false);
        }
        else if (gSuppressingInput) {
            // Once more on release, so input accumulated while suppressed does
            // not all arrive the moment control comes back.
            SDK::ClearLocalInput(true);
        }

        gSuppressingInput = suppress;
    }

    bool IsGameInputSuppressed() {
        return gSuppressingInput;
    }
} // namespace VantaMP::Game
