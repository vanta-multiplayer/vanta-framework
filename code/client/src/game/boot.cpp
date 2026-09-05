#include "boot.h"

#include "sdk/patterns.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

#include <atomic>

namespace VantaMP::Game {
    namespace {
        // gGameState values. The game's own enum; only the two Vanta drives.
        constexpr int kGameStateInitOnce = 5;

        // NOTE: reaching "playing" does NOT mean the world, streaming or the
        // entity pools exist. CGame::Init3 returning is the real signal, which
        // is why the two flags below are separate.
        constexpr int kGameStatePlaying = 8;

        std::atomic_bool gEngineInitialised {false};
        std::atomic_bool gFrontendBypassed {false};

        using VoidFn = void(__cdecl *)();

        // CGame::Init3 takes the data file to load, pushed by its caller. Called
        // through a plain pointer rather than plugin-sdk's wrapper: the SDK's
        // headers are confined to one translation unit, and a cdecl call to a
        // resolved address needs none of them.
        using Init3Fn = void(__cdecl *)(const char *);

        Init3Fn gOriginalInit3      = nullptr;
        VoidFn gOriginalMenuProcess = nullptr;

        // THE ARGUMENT IS ALREADY ON THE STACK, and stays there.
        //
        // The call site pushes the data-file name and cleans up after itself
        // (`push esi` ... `add esp, 4`), so this takes it as a parameter and
        // passes it through rather than inventing one. A hook declared as
        // taking nothing would leave that push unmatched and hand Init3
        // whatever happened to be on top of the stack.
        void __cdecl Init3_Hook(const char *dataFile) {
            Framework::Logging::GetLogger("Boot")->info("Initialising the game engine from {}", dataFile ? dataFile : "(null)");

            gOriginalInit3(dataFile);

            gEngineInitialised.store(true, std::memory_order_release);
            Framework::Logging::GetLogger("Boot")->info("Game engine initialised; the world exists");
        }

        // CLoadingScreen::Draw, replaced wholesale.
        //
        // STILL ADVANCES THE CHUNK COUNTER. The loader treats each call as one
        // more piece of the world loaded and waits for the count to complete,
        // so a version of this that merely draws nothing never finishes
        // loading. That is the trap this replaces rather than removes.
        using NewChunkFn = void(__cdecl *)();
        NewChunkFn gNewChunkLoaded = nullptr;

        void __cdecl LoadingScreen_Replacement(const char *, const char *) {
            if (gNewChunkLoaded) {
                gNewChunkLoaded();
            }
        }

        // The frontend's per-frame call, wrapped.
        //
        // On the first frame it clears both pause flags and moves the game into
        // the playing state, then lets the original run. The original here sets
        // the menu's render states, and is kept because the frontend keeps
        // drawing for a frame or two after the state changes - dropping it
        // leaves the device configured for whatever ran last.
        void MenuProcess_Hook() {
            if (!gFrontendBypassed.load(std::memory_order_acquire)) {
                gFrontendBypassed.store(true, std::memory_order_release);

                const auto &p = SDK::gPatterns;

                hook::put<std::uint8_t>(p.CTimer__UserPause, 0);
                hook::put<std::uint8_t>(p.CMenuManager__Pause, 0);
                hook::put<int>(p.gGameState, kGameStatePlaying);

                Framework::Logging::GetLogger("Boot")->info("Frontend bypassed; the game is playing");
            }

            if (gOriginalMenuProcess) {
                gOriginalMenuProcess();
            }
        }
    } // namespace

    void ApplyBootPatches() {
        const auto &p = SDK::gPatterns;

        // --- Engine initialisation --------------------------------------------

        // Wrapped rather than replaced: this is where the world is built, and
        // Vanta only wants to know when it finished.
        gOriginalInit3 = hook::get_call(reinterpret_cast<Init3Fn>(p.CGame__Init3Call));
        hook::call(p.CGame__Init3Call, Init3_Hook);

        // --- Loading screens ---------------------------------------------------

        // Start at "initialise once" instead of zero, skipping the early
        // startup states a single-player game walks through.
        hook::nop(p.CGame__StateResetToZero, 6);
        hook::put<int>(p.gGameState, kGameStateInitOnce);

        // CLoadingScreen::NewChunkLoaded itself, not a call to it. The two
        // draw sites patched below live INSIDE this function, so taking its
        // address from one of them would resolve to the artwork routine they
        // call rather than to the counter the loader is waiting on.
        gNewChunkLoaded = reinterpret_cast<NewChunkFn>(p.CLoadingScreen__NewChunk);

        hook::jump(p.CLoadingScreen__Draw, LoadingScreen_Replacement);

        // No loading-screen audio sting.
        hook::nop(p.CLoadingScreen__AudioCall, 5);

        // And no loading artwork.
        //
        // SEPARATE FROM THE COPYRIGHT SCREEN, though they run back to back. The
        // notice stays; the progress bars and splash images between it and the
        // world are a single-player sequence a multiplayer client replaces with
        // its own connect flow.
        //
        // The third site RETURNS rather than being skipped: the code after it
        // assumes the draw happened, so NOPping the call the way the other two
        // are handled leaves it running on state that was never set up.
        hook::nop(p.CLoadingScreen__DrawCall1, 5);
        hook::nop(p.CLoadingScreen__DrawCall2, 5);
        hook::return_function(p.CLoadingScreen__DrawReturn);

        // --- Frontend ----------------------------------------------------------

        gOriginalMenuProcess = hook::get_call(reinterpret_cast<VoidFn>(p.CMenuManager__ProcessCall));
        hook::call(p.CMenuManager__ProcessCall, MenuProcess_Hook);

        // Its drawing, its input and its background - all of it belongs to a
        // menu the player never sees.
        hook::nop(p.CMenuManager__DrawStandardMenus, 6);
        hook::nop(p.CMenuManager__InputCall, 5);
        hook::nop(p.CMenuManager__BackgroundRect, 5);

        // jz -> jmp, so the background texture branch is never taken.
        hook::put<std::uint8_t>(p.CMenuManager__BackgroundTexture, 0xEB);

        // Escape must not reopen the menu, and losing window focus must not
        // pause a game other people are still playing.
        hook::put<std::uint8_t>(p.CMenuManager__EscapeHandler1, 0xEB);
        hook::put<std::uint8_t>(p.CMenuManager__EscapeHandler2, 0xEB);
        hook::nop(p.CMenuManager__PauseCheck, 6);
        hook::return_function(p.CMenuManager__OnFocusLoss);
        hook::put<std::uint8_t>(p.CMenuManager__AfterAltTabFlag, 0x00);

        // --- Console chatter ----------------------------------------------------

        hook::nop(p.Print__AspectRatio, 5);
        hook::nop(p.Print__SoundManager, 5);

        Framework::Logging::GetLogger("Boot")->info("Boot patches applied; the frontend will be bypassed on its first frame");
    }

    bool IsEngineInitialised() {
        return gEngineInitialised.load(std::memory_order_acquire);
    }

    bool IsFrontendBypassed() {
        return gFrontendBypassed.load(std::memory_order_acquire);
    }
} // namespace VantaMP::Game
