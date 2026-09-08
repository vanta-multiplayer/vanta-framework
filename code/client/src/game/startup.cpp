#include "startup.h"

#include "core/client_config.h"
#include "core/diagnostics/breadcrumbs.h"
#include "game/entities/player_slots.h"
#include "game/entities/remote_players.h"
#include "game/patches/audio.h"
#include "game/patches/boot.h"
#include "game/patches/display.h"
#include "game/patches/gameplay.h"
#include "game/patches/input.h"
#include "game/patches/population.h"
#include "game/patches/script.h"
#include "game/patches/vehicle_lights.h"
#include "game/sync/death.h"
#include "game/ui/nametags.h"
#include "sdk/game_build.h"
#include "sdk/patterns.h"
#include "sdk/vehicle/vehicle.h"
#include "sdk/vehicle/occupancy.h"

namespace VantaMP::Game {
    namespace {
        void Apply(const char *name, void (*fn)()) {
            Diagnostics::Breadcrumbs::SetLastPatch(name);
            fn();
        }
    } // namespace

    void Start() {
        const auto &config = Core::GetClientConfig();

        // Before anything draws. The display module is consulted from the
        // CreateDevice hook, which the game can reach as soon as its first
        // frame - there is no later point that is still early enough.
        SetDisplaySettings({config.displayWidth, config.displayHeight, config.displayBorderless, config.displayExclusive});

        // Recorded rather than enforced: the scans below decide whether this
        // executable is usable, and they say so loudly if it is not. Knowing
        // the build first only makes that failure legible.
        Diagnostics::Breadcrumbs::SetStage(Diagnostics::Stage::GameIdentified);
        (void)SDK::IdentifyGameBuild();
        SDK::Patterns::InitPatterns();

        // FIRST OF THE PATCHES. GTA's single-instance guard runs at the very
        // top of WinMain, which the launcher resumes the moment InitClient
        // returns - so anything applied after the other patches has already
        // lost the race.
        Apply("instances", AllowMultipleInstances);

        // FIRST OF THE WORLD PATCHES, because it rewrites the code every later
        // patch is written into. Running it afterwards would scan over bytes
        // Vanta had already changed, and any of those that happened to look
        // like an array reference would be rewritten a second time.
        Diagnostics::Breadcrumbs::SetStage(Diagnostics::Stage::PatchesApplying);
        Apply("player slots", ExpandPlayerSlots);

        // Before the game gets a chance to populate the world. GTA starts
        // inventing pedestrians and traffic as soon as it streams anything in,
        // and a ped that already exists has to be deleted rather than never
        // created.
        Apply("population", DisableAutonomousPopulation);

        // Everything above runs before the game has drawn a frame, which is
        // what makes bypassing its frontend possible at all: these rewrite
        // call sites the game has not reached yet.
        SetFrameLimitOnStart(static_cast<int>(config.frameLimit));
        Apply("boot", ApplyBootPatches);
        Apply("audio", ApplyAudioPatches);

        // Last of the startup patches: this one disables the mission script
        // and takes over its per-frame call, which is where the local player
        // is created once the world is complete.
        Apply("script", ApplyScriptPatches);

        // After the slots exist, since the hooks index into them.
        Diagnostics::Breadcrumbs::SetStage(Diagnostics::Stage::HooksInstalling);
        Apply("remote players", InstallRemotePlayerHooks);
        Apply("nametags", InstallNametags);

        // Has to land before the game acquires the mouse, which it does during
        // its own startup - from a frame callback this would already be too late.
        Apply("input", ReleaseMouseCapture);
        Apply("pad suppress", InstallPadSuppressHook);
        Apply("vehicle explode", SDK::InstallVehicleExplosionHooks);
        Apply("vehicle colour", SDK::InstallVehicleColorHooks);
        Apply("vehicle headlights", SDK::InstallVehicleHeadlightHooks);
        Apply("gameplay", ApplyGameplayPatches);

        // WEAPONTYPE_EXPLOSION. Occupancy marks the ped dead; this is
        // what the death packet names so a resource sees the tank, not
        // "unknown".
        SDK::SetVehicleExplosionDeathHandler([] {
            RememberLocalDamager(0, 51);
        });

        // Unlocks the wider modes. The projection itself is corrected later,
        // once there is a window whose shape can be measured.
        Apply("widescreen", ApplyWidescreenPatches);

        // Before WinMain reaches RW engine init. psSelectDevice is what
        // pops GTA's Device Selection dialog; the hook has to be in
        // before that call or first setup never sees it.
        Apply("device selection", InstallDeviceSelectionHook);

        Diagnostics::Breadcrumbs::SetStage(Diagnostics::Stage::WaitingForFrontend);
    }
} // namespace VantaMP::Game
