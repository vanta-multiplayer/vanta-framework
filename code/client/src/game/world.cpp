#include "world.h"

#include "player_sync.h"
#include "remote_players.h"
#include "sdk/patterns.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

#include <atomic>

namespace VantaMP::Game {
    namespace {
        // The direction flag CCamera::Fade takes.
        //
        // THE SENSE IS THE OPPOSITE OF WHAT THE CALL SITES SUGGEST, and it was
        // got wrong once by assuming 1 meant "fade out". Read out of
        // CCamera::ProcessFade, which is what actually drives the alpha:
        //
        //   cmp  ax, 1          ; the flag
        //   jne  <below>        ; NOT 1 -> this branch
        //   ...
        //   mov  edx, 0x437F0000 ; 255.0f, fully opaque
        //
        // The opaque end is loaded where the flag is NOT 1, so 0 fades TO the
        // fade colour and 1 fades back to the scene.
        constexpr short kFadeOut = 0;
        constexpr short kFadeIn  = 1;

        // CJ. The model the game itself uses for the player.
        constexpr std::uint8_t kPlayerModelId = 7;

        std::atomic_bool gPlayerSpawned {false};
        SpawnPoint gSpawnPoint;
        void (*gFrameCallback)() = nullptr;

        using SetupPlayerFn = void(__cdecl *)(int);
        using FindPlayerFn  = void *(__cdecl *)(int);
        using LoadSceneFn   = void(__cdecl *)(const float *);
        using SetPosnFn     = void(__thiscall *)(void *, float, float, float);

        // Both are __thiscall on TheCamera, so the object goes in ecx.
        using FadeFn           = void(__thiscall *)(void *, float, short);
        using SetFadeColourFn  = void(__thiscall *)(void *, std::uint8_t, std::uint8_t, std::uint8_t);

        void *Camera() {
            return reinterpret_cast<void *>(SDK::gPatterns.TheCamera);
        }

        void Fade(float durationSeconds, short direction) {
            reinterpret_cast<FadeFn>(SDK::gPatterns.CCamera__Fade)(Camera(), durationSeconds, direction);
        }

        // Creates the local player, once, and puts it somewhere.
        void SpawnLocalPlayer() {
            const auto &p = SDK::gPatterns;

            // BLACK FIRST, PLAYER SECOND.
            //
            // The ped has to exist before the world is usable, but a client that
            // has not joined a server has nothing truthful to show: no spawn
            // point, no model choice, no position that means anything. Creating
            // it in view briefly presents a default CJ standing in Los Santos,
            // which reads as "you are playing" when the connect menu is the
            // actual state.
            //
            // Duration 0, not a timed fade: this has to be black on the very
            // first frame the ped could be visible on, and a fade with a
            // duration is still part transparent while it runs.
            reinterpret_cast<SetFadeColourFn>(p.CCamera__SetFadeColour)(Camera(), 0, 0, 0);
            Fade(0.0f, kFadeOut);

            // The model id SetupPlayerPed pushes. Patched rather than passed:
            // the id is baked into the instruction, not taken as an argument.
            hook::put<std::uint8_t>(p.CPlayerPed__ModelIdPush + 1, kPlayerModelId);

            reinterpret_cast<SetupPlayerFn>(p.CPlayerPed__SetupPlayerPed)(0);
            hook::put<int>(p.CWorld__PlayerInFocus, 0);

            const auto player = reinterpret_cast<FindPlayerFn>(p.FindPlayerPed)(0);

            if (!player) {
                Framework::Logging::GetLogger("World")->error("The local player was not created; FindPlayerPed returned nothing");
                return;
            }

            // Through the game's own SetPosn rather than by writing the matrix.
            // A ped carries both a matrix and a simple transform and the two
            // have to agree; SetPosn is what keeps them consistent, and
            // reimplementing that from offsets is how a player ends up visible
            // in one place and collidable in another.
            reinterpret_cast<SetPosnFn>(p.CPlaceable__SetPosn)(player, gSpawnPoint.x, gSpawnPoint.y, gSpawnPoint.z);

            // Streams the world in around where the player now is, so the first
            // visible frame is not an empty landscape.
            const float position[3] = {gSpawnPoint.x, gSpawnPoint.y, gSpawnPoint.z};
            reinterpret_cast<LoadSceneFn>(p.CStreaming__LoadScene)(position);

            // Slot 0 is the local player. The hooks would fall back to it
            // anyway for a ped they do not recognise, but claiming the slot
            // makes that the answer rather than the default - and it is what
            // tells SlotOfPed the difference between "the local player" and "a
            // ped Vanta does not own".
            RegisterRemotePlayer(kLocalPlayerSlot, player);

            gPlayerSpawned.store(true, std::memory_order_release);

            Framework::Logging::GetLogger("World")->info("Local player created at {:.1f}, {:.1f}, {:.1f} behind a black screen", gSpawnPoint.x, gSpawnPoint.y, gSpawnPoint.z);
        }

        // Replaces CRunningScript::Process.
        //
        // The SCM is disabled, so this call is free - and it is the right place
        // for per-frame work: it runs once a frame during gameplay, and the
        // first time it runs is the moment the world is complete.
        void ScriptProcess_Hook() {
            static bool spawned = false;

            if (!spawned) {
                spawned = true;
                SpawnLocalPlayer();
            }

            // The network, once per frame and on the game thread - which is
            // the only thread allowed to touch peds.
            UpdatePlayerSync();

            // And a step of correction towards where everyone should be. State
            // arrives ten times a second and the game draws sixty, so this is
            // what turns six-frame-apart positions into movement.
            InterpolateRemotePlayers();

            // Guarded, because a throwing callback must not unwind into the
            // game's own frame processing, where nothing would catch it.
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
            Framework::Logging::GetLogger("World")->error("SCM skip is {} bytes away, too far for a short jump", displacement);
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

        Framework::Logging::GetLogger("World")->info("Mission script disabled; its per-frame call now drives the client");
    }

    void SetSpawnPoint(const SpawnPoint &point) {
        gSpawnPoint = point;
    }

    bool IsPlayerSpawned() {
        return gPlayerSpawned.load(std::memory_order_acquire);
    }

    void FadeIn(float durationSeconds) {
        // No colour set here: it is whatever the last fade out chose, and
        // resetting it to black would be indistinguishable from a fade that
        // never ran on the frames where alpha is still high.
        Fade(durationSeconds, kFadeIn);
        Framework::Logging::GetLogger("World")->info("Revealing the world over {:.2f}s", durationSeconds);
    }

    void FadeOut(float durationSeconds) {
        reinterpret_cast<SetFadeColourFn>(SDK::gPatterns.CCamera__SetFadeColour)(Camera(), 0, 0, 0);
        Fade(durationSeconds, kFadeOut);
        Framework::Logging::GetLogger("World")->info("Hiding the world over {:.2f}s", durationSeconds);
    }

    void ApplyLocalSpawn(float x, float y, float z, float heading, float health, float armour) {
        const auto &p = SDK::gPatterns;

        const auto player = reinterpret_cast<FindPlayerFn>(p.FindPlayerPed)(0);

        if (!player) {
            Framework::Logging::GetLogger("World")->error("A spawn arrived before the local player existed");
            return;
        }

        reinterpret_cast<SetPosnFn>(p.CPlaceable__SetPosn)(player, x, y, z);

        // Streams the world in around the new position, so the first visible
        // frame is not an empty landscape.
        const float position[3] = {x, y, z};
        reinterpret_cast<LoadSceneFn>(p.CStreaming__LoadScene)(position);

        static bool revealed = false;

        if (!revealed) {
            revealed = true;

            Framework::Logging::GetLogger("World")->info("Spawned by the server at {:.1f}, {:.1f}, {:.1f}", x, y, z);

            FadeIn();
        }
    }

    void SetFrameCallback(void (*callback)()) {
        gFrameCallback = callback;
    }
} // namespace VantaMP::Game
