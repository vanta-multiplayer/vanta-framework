#include "script.h"

#include "game/entities/local_player.h"
#include "game/patches/input.h"
#include "game/sync/player_sync.h"
#include "game/sync/shots.h"
#include "sdk/patterns.h"
#include "sdk/ped/aim.h"
#include "sdk/ped/weapon.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

namespace VantaMP::Game {
    namespace {
        void (*gFrameCallback)() = nullptr;

        // Replaces CRunningScript::Process.
        //
        // The SCM is disabled, so this call is free - and it is the right place
        // for per-frame work: it runs once a frame during gameplay, and the
        // first time it runs is the moment the world is complete.
        void ScriptProcess_Hook() {
            static bool spawned = false;

            if (!spawned) {
                spawned = true;
                CreateLocalPlayer();

                // Correction runs per ped from the ProcessControl hook rather
                // than from here, so it sees the position the game's own walk
                // produced instead of overwriting it.
                InstallRemotePlayerCorrection();

                // Aim is corrected per ped too, at the point the game works out
                // where to point a gun. Installed here rather than with the
                // startup patches because it only matters once there are peds.
                SDK::InstallAimSync();

                // Shots and reloads are their own packets, installed here for
                // the same reason as aim: they only matter once there are peds,
                // and MinHook has already enabled the startup hooks by then.
                InstallShotPackets();
            }

            // The network, once per frame and on the game thread - which is the
            // only thread allowed to touch peds.
            UpdatePlayerSync();

            // GTA has no reload key. R is the SA-MP/MTA bind, edge-detected so
            // holding it does not restart the animation every frame.
            static bool reloadHeld = false;
            const bool reloadDown  = (GetAsyncKeyState('R') & 0x8000) != 0;
            const bool reloadPress = reloadDown && !reloadHeld;

            reloadHeld = reloadDown;

            if (reloadPress && !IsGameInputSuppressed()) {
                SDK::ReloadLocalPedWeapon();
            }

            // Reports the rising edge, whether R started it or the clip ran out.
            CheckLocalReload();

            if (gFrameCallback) {
                gFrameCallback();
            }
        }
    } // namespace

    void ApplyScriptPatches() {
        const auto &p = SDK::gPatterns;

        // The interpreter, stubbed.
        hook::return_function(p.CRunningScript__ProcessOneCommand);

        // And its opcode table, so nothing can dispatch through it even if the
        // interpreter were somehow reached.
        hook::nop(p.CRunningScript__OpcodeTable, 0x6C);

        // Don't load the SCM at all.
        //
        // `push <script name>` becomes a short jump over the load. The distance
        // is 0x32, which fits the one signed byte a short jump carries - if a
        // future build moves these apart this stops fitting, and it will fail
        // loudly here rather than jumping somewhere arbitrary.
        const auto displacement = static_cast<std::intptr_t>(p.ScmLoadSkipTarget) - static_cast<std::intptr_t>(p.ScmLoadJump + 2);

        if (displacement < -128 || displacement > 127) {
            Framework::Logging::GetLogger("Script")->error("SCM skip is {} bytes away, too far for a short jump", displacement);
        }
        else {
            hook::put<std::uint8_t>(p.ScmLoadJump, 0xEB);
            hook::put<std::int8_t>(p.ScmLoadJump + 1, static_cast<std::int8_t>(displacement));
        }

        // REPLACED, NOT WRAPPED. Every other hook in the client calls the
        // original afterwards; this one must not. The original walks the loaded
        // scripts and runs them, and the whole point of the patches above is
        // that there are none - calling it would be asking a disabled
        // interpreter to interpret a script that was never loaded.
        hook::call(p.CRunningScript__ProcessCall, ScriptProcess_Hook);

        Framework::Logging::GetLogger("Script")->info("Mission script disabled; its per-frame call now drives the client");
    }

    void SetFrameCallback(void (*callback)()) {
        gFrameCallback = callback;
    }
} // namespace VantaMP::Game
