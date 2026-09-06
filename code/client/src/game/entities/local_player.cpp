#include "local_player.h"

#include "game/camera.h"
#include "game/entities/remote_players.h"
#include "core/diagnostics/breadcrumbs.h"
#include "sdk/patterns.h"
#include "shared/spawn_points.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

#include <atomic>

namespace VantaMP::Game {
    namespace {
        // CJ. The model the game itself uses for the player.
        constexpr std::uint8_t kPlayerModelId = 7;

        std::atomic_bool gPlayerSpawned {false};
        // The placeholder the ped is created at, before a server has an
        // opinion. Shared with the server so the two agree - the interest grid
        // measures from the server's copy, and a client streaming around the
        // origin while the server thinks it is in Ganton is how players end up
        // invisible to each other.
        SpawnPoint gSpawnPoint {Shared::Spawn::kDefaultX, Shared::Spawn::kDefaultY, Shared::Spawn::kDefaultZ};

        using SetupPlayerFn = void(__cdecl *)(int);
        using FindPlayerFn  = void *(__cdecl *)(int);
        using LoadSceneFn   = void(__cdecl *)(const float *);
        using SetPosnFn     = void(__thiscall *)(void *, float, float, float);
    } // namespace

    void CreateLocalPlayer() {
        Diagnostics::Breadcrumbs::SetStage(Diagnostics::Stage::WaitingForPlayer);

        // Named because this is the riskiest call in startup: it builds a ped
        // through the game's own path, and a crash inside it says nothing about
        // who asked.
        VANTAMP_OPERATION("creating the local player");

        const auto &p = SDK::gPatterns;

        // BLACK FIRST, PLAYER SECOND.
        //
        // The ped has to exist before the world is usable, but a client that has
        // not joined a server has nothing truthful to show: no spawn point, no
        // model choice, no position that means anything. Creating it in view
        // briefly presents a default CJ standing in Los Santos, which reads as
        // "you are playing" when the connect menu is the actual state.
        //
        // Duration 0, not a timed fade: this has to be black on the very first
        // frame the ped could be visible on, and a fade with a duration is still
        // part transparent while it runs.
        FadeOut(0.0f);

        // The model id SetupPlayerPed pushes. Patched rather than passed: the id
        // is baked into the instruction, not taken as an argument.
        hook::put<std::uint8_t>(p.CPlayerPed__ModelIdPush + 1, kPlayerModelId);

        reinterpret_cast<SetupPlayerFn>(p.CPlayerPed__SetupPlayerPed)(0);
        hook::put<int>(p.CWorld__PlayerInFocus, 0);

        const auto player = reinterpret_cast<FindPlayerFn>(p.FindPlayerPed)(0);

        if (!player) {
            Framework::Logging::GetLogger("Player")->error("The local player was not created; FindPlayerPed returned nothing");
            return;
        }

        // Through the game's own SetPosn rather than by writing the matrix. A
        // ped carries both a matrix and a simple transform which have to agree,
        // and reimplementing that from offsets is how a player ends up visible
        // in one place and collidable in another.
        reinterpret_cast<SetPosnFn>(p.CPlaceable__SetPosn)(player, gSpawnPoint.x, gSpawnPoint.y, gSpawnPoint.z);

        // Streams the world in around where the player now is, so the first
        // visible frame is not an empty landscape.
        const float position[3] = {gSpawnPoint.x, gSpawnPoint.y, gSpawnPoint.z};
        reinterpret_cast<LoadSceneFn>(p.CStreaming__LoadScene)(position);

        // Slot 0 is the local player. The hooks would fall back to it anyway for
        // a ped they do not recognise, but claiming the slot makes that the
        // answer rather than the default - and it is what tells SlotOfPed the
        // difference between "the local player" and "a ped Vanta does not own".
        RegisterRemotePlayer(kLocalPlayerSlot, player);

        gPlayerSpawned.store(true, std::memory_order_release);

        Diagnostics::Breadcrumbs::SetStage(Diagnostics::Stage::PlayerReady);

        Framework::Logging::GetLogger("Player")->info("Local player created at {:.1f}, {:.1f}, {:.1f} behind a black screen", gSpawnPoint.x, gSpawnPoint.y, gSpawnPoint.z);
    }

    bool IsPlayerSpawned() {
        return gPlayerSpawned.load(std::memory_order_acquire);
    }

    void SetSpawnPoint(const SpawnPoint &point) {
        gSpawnPoint = point;
    }

    void ApplyLocalSpawn(float x, float y, float z, float heading, float health, float armour) {
        const auto &p = SDK::gPatterns;

        const auto player = reinterpret_cast<FindPlayerFn>(p.FindPlayerPed)(0);

        if (!player) {
            Framework::Logging::GetLogger("Player")->error("A spawn arrived before the local player existed");
            return;
        }

        reinterpret_cast<SetPosnFn>(p.CPlaceable__SetPosn)(player, x, y, z);

        const float position[3] = {x, y, z};
        reinterpret_cast<LoadSceneFn>(p.CStreaming__LoadScene)(position);

        static bool revealed = false;

        if (!revealed) {
            revealed = true;

            Framework::Logging::GetLogger("Player")->info("Spawned by the server at {:.1f}, {:.1f}, {:.1f}", x, y, z);

            FadeIn();
        }
    }
} // namespace VantaMP::Game
