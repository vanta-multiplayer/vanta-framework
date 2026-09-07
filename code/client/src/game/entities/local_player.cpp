#include "local_player.h"

#include "game/camera.h"
#include "game/entities/remote_players.h"
#include "core/diagnostics/breadcrumbs.h"
#include "sdk/patterns.h"
#include "game/sync/death.h"
#include "game/sync/player_sync.h"
#include "sdk/ped/local_ped.h"
#include "shared/world/world_space.h"
#include "sdk/ped/weapon.h"
#include "sdk/vehicle/occupancy.h"
#include "shared/world/spawn_points.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

#include <atomic>

namespace VantaMP::Game {
    namespace {
        // CJ. The model the game itself uses for the player.
        constexpr std::uint8_t kPlayerModelId = 7;

        // WEAPONTYPE_AK47 and enough to see a reload. Named rather than spelled
        // into the call so the testing loadout below is one thing to delete.
        constexpr unsigned int kTestWeaponType = 30;
        constexpr unsigned int kTestWeaponAmmo = 300;

        std::atomic_bool gPlayerSpawned {false};

        // CreateLocalPlayer starts faded out. Disconnect sets this again so a
        // later spawn can FadeIn; an in-session respawn leaves it false.
        bool gWorldConcealed = true;
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

        void *FindLocalPed() {
            return reinterpret_cast<FindPlayerFn>(SDK::gPatterns.FindPlayerPed)(0);
        }

        void PlaceLocalPed(void *player, float x, float y, float z) {
            const auto &p = SDK::gPatterns;

            reinterpret_cast<SetPosnFn>(p.CPlaceable__SetPosn)(player, x, y, z);

            const float position[3] = {x, y, z};
            reinterpret_cast<LoadSceneFn>(p.CStreaming__LoadScene)(position);
        }

        void ClaimLocalPed(void *player) {
            hook::put<int>(SDK::gPatterns.CWorld__PlayerInFocus, 0);
            RegisterRemotePlayer(kLocalPlayerSlot, player);
        }
    } // namespace

    void CreateLocalPlayer() {
        if (auto *existing = FindLocalPed()) {
            ClaimLocalPed(existing);
            return;
        }

        Diagnostics::Breadcrumbs::SetStage(Diagnostics::Stage::WaitingForPlayer);

        VANTAMP_OPERATION("creating the local player");

        const auto &p = SDK::gPatterns;

        // BLACK FIRST. The ped has to exist or CEntity::RegisterReference
        // (0x571B70) reads a null player and dies. Nobody should see default
        // CJ in Los Santos while the connect menu is up.
        FadeOut(0.0f);

        hook::put<std::uint8_t>(p.CPlayerPed__ModelIdPush + 1, kPlayerModelId);
        reinterpret_cast<SetupPlayerFn>(p.CPlayerPed__SetupPlayerPed)(0);

        const auto player = FindLocalPed();

        if (!player) {
            Framework::Logging::GetLogger("Player")->error("The local player was not created; FindPlayerPed returned nothing");
            return;
        }

        PlaceLocalPed(player, gSpawnPoint.x, gSpawnPoint.y, gSpawnPoint.z);
        ClaimLocalPed(player);

        Framework::Logging::GetLogger("Player")->info("Local player created at {:.1f}, {:.1f}, {:.1f} behind a black screen", gSpawnPoint.x, gSpawnPoint.y, gSpawnPoint.z);
    }

    void DestroyLocalPlayer() {
        ConcealWorld();

        // Always out of the seat first. The ped exists from the first
        // gameplay frame, so they can be in a car before a spawn, and
        // deleting that car with them still named as driver is a crash.
        // Pointer detach only - dead, wrecked, or mid-explode must not
        // run SetPedOut. Then stand them up at the placeholder so the
        // next server is not joining onto a corpse.
        if (FindLocalPed()) {
            SDK::UnlinkFromVehicle(kLocalPlayerSlot);
            SDK::ReviveLocalPed(gSpawnPoint.x, gSpawnPoint.y, gSpawnPoint.z, 0.0f, 100.0f, 0.0f);
        }

        if (!gPlayerSpawned.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        UnregisterRemotePlayer(kLocalPlayerSlot);

        Framework::Logging::GetLogger("Player")->info("Local player session ended; the GTA ped stays");
    }

    bool IsPlayerSpawned() {
        return gPlayerSpawned.load(std::memory_order_acquire);
    }

    void SetSpawnPoint(const SpawnPoint &point) {
        gSpawnPoint = point;
    }

    void ApplyLocalSpawn(float x, float y, float z, float heading, float health, float armour, std::uint8_t interior, std::uint16_t dimension) {
        CreateLocalPlayer();

        if (!FindLocalPed()) {
            Framework::Logging::GetLogger("Player")->error("A spawn arrived and the local player could not be created");
            return;
        }

        gPlayerSpawned.store(true, std::memory_order_release);
        Diagnostics::Breadcrumbs::SetStage(Diagnostics::Stage::PlayerReady);

        // Health and heading used to be ignored, so a respawn left the ped
        // dead on the ground at the new coordinates. Revive first, then
        // stream the area, then arm them - SetInitialState strips weapons.
        SDK::ReviveLocalPed(x, y, z, heading, health, armour);
        ApplyOwnedWorldSpace(interior, dimension, Shared::kWorldSpaceInterior | Shared::kWorldSpaceDimension);
        ClearLocalDeathReport();

        const float position[3] = {x, y, z};
        reinterpret_cast<LoadSceneFn>(SDK::gPatterns.CStreaming__LoadScene)(position);

        // A TESTING LOADOUT, not a design decision - the same kind of thing
        // as the waterfront spawn point. Weapon sync cannot be looked at
        // with two empty-handed players and there is nothing to pick up
        // where they spawn. Goes when a resource decides what a player
        // spawns holding.
        //
        // HERE AND NOT IN CreateLocalPlayer, which is the earlier and more
        // obvious home for it. The ped is built before the engine reads
        // GTA.DAT, so at that point there is no streamer to ask for the
        // weapon's model - the ped exists, the world does not. By the time
        // the server has said where to spawn, both do.
        SDK::GiveLocalPedWeapon(kTestWeaponType, kTestWeaponAmmo);

        if (gWorldConcealed) {
            gWorldConcealed = false;
            FadeIn();
        }

        Framework::Logging::GetLogger("Player")->info("Spawned by the server at {:.1f}, {:.1f}, {:.1f}", x, y, z);
    }

    void ConcealWorld() {
        gWorldConcealed = true;
        FadeOut();
    }
} // namespace VantaMP::Game
