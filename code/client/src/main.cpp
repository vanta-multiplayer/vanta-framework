// Include safe_win32 FIRST to avoid winsock conflicts
#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>
#include <utils/string_utils.h>

#include "core/client_config.h"
#include "core/diagnostics/breadcrumbs.h"
#include "core/diagnostics/crash_report.h"
#include "core/globals.h"
#include "game/patches/boot.h"
#include "game/patches/display.h"
#include "game/patches/gameplay.h"
#include "game/patches/input.h"
#include "game/entities/player_slots.h"
#include "game/entities/remote_players.h"
#include "game/patches/population.h"
#include "game/entities/local_player.h"
#include "game/patches/script.h"
#include "game/ui/nametags.h"
#include "sdk/game_build.h"
#include "sdk/patterns.h"

namespace VantaMP::Core {
    std::string gProjectPath;
} // namespace VantaMP::Core

extern "C" void __declspec(dllexport) InitClient(const wchar_t *projectPath) {
    VantaMP::Core::gProjectPath = Framework::Utils::StringUtils::WideToNormal(projectPath);

    // ONE LOG PER INSTANCE. Two clients on one machine is the ordinary way to
    // test anything involving a second player, and a single fixed name means
    // they share one rotating file sink - so the lines interleave, or one
    // instance truncates the other's log.
    //
    // Keyed by process id rather than by counting instances: a count is racy
    // when two clients start together, and a name that depends on launch order
    // is worse than one that is simply unique.
    Framework::Logging::GetInstance()->SetLogName(fmt::format("VantaMP-{}", GetCurrentProcessId()));
    Framework::Logging::GetInstance()->SetLogFolder(fmt::format("{}/logs", VantaMP::Core::gProjectPath));

    // BEFORE ANY PATCHING, because the patch burst below is where a crash is
    // both most likely and least self-explanatory - it runs against a game the
    // signatures may have misidentified. Installed this early it costs a
    // filter registration and catches everything after it.
    VantaMP::Diagnostics::CrashReport::Install(fmt::format("{}/crashes", VantaMP::Core::gProjectPath));

    // Before anything reads a setting. The display module in particular is
    // consulted from the CreateDevice hook, which the game can reach as soon as
    // its first frame - there is no later point that is still early enough.
    const auto &config = VantaMP::Core::LoadClientConfig(VantaMP::Core::gProjectPath);

    VantaMP::Game::SetDisplaySettings({config.displayWidth, config.displayHeight, config.displayBorderless});

    MH_Initialize();
    hook::set_base();

    // Which build we are in. Recorded rather than enforced: the scans below
    // decide whether this executable is usable, and they say so loudly if it is
    // not. Knowing the build first only makes that failure legible.
    VantaMP::Diagnostics::Breadcrumbs::SetStage(VantaMP::Diagnostics::Stage::GameIdentified);

    (void)VantaMP::SDK::IdentifyGameBuild();

    VantaMP::SDK::Patterns::InitPatterns();

    // FIRST OF THE PATCHES. GTA's single-instance guard runs at the very top of
    // WinMain, which the launcher resumes the moment this returns - so anything
    // applied after the other patches has already lost the race.
    VantaMP::Game::AllowMultipleInstances();

    // FIRST OF THE WORLD PATCHES, because it rewrites the code every later
    // patch is written into. Running it afterwards would scan over bytes Vanta
    // had already changed, and any of those that happened to look like an array
    // reference would be rewritten a second time.
    VantaMP::Diagnostics::Breadcrumbs::SetStage(VantaMP::Diagnostics::Stage::PatchesApplying);

    VantaMP::Diagnostics::Breadcrumbs::SetLastPatch("player slots");
    VantaMP::Game::ExpandPlayerSlots();

    // Before the game gets a chance to populate the world. GTA starts inventing
    // pedestrians and traffic as soon as it streams anything in, and a ped that
    // already exists has to be deleted rather than never created - which fights
    // the very lifetime rules this is establishing.
    VantaMP::Diagnostics::Breadcrumbs::SetLastPatch("population");
    VantaMP::Game::DisableAutonomousPopulation();

    // Everything above runs before the game has drawn a frame, which is what
    // makes bypassing its frontend possible at all: these rewrite call sites
    // the game has not reached yet.
    VantaMP::Game::SetFrameLimitOnStart(static_cast<int>(config.frameLimit));

    VantaMP::Diagnostics::Breadcrumbs::SetLastPatch("boot");
    VantaMP::Game::ApplyBootPatches();

    // Last of the startup patches: this one disables the mission script and
    // takes over its per-frame call, which is where the local player is
    // created once the world is complete.
    VantaMP::Diagnostics::Breadcrumbs::SetLastPatch("script");
    VantaMP::Game::ApplyScriptPatches();

    // After the slots exist, since the hooks index into them.
    VantaMP::Diagnostics::Breadcrumbs::SetStage(VantaMP::Diagnostics::Stage::HooksInstalling);

    VantaMP::Game::InstallRemotePlayerHooks();

    VantaMP::Diagnostics::Breadcrumbs::SetLastPatch("nametags");
    VantaMP::Game::InstallNametags();

    // Has to land before the game acquires the mouse, which it does during its
    // own startup - from a frame callback this would already be too late.
    VantaMP::Diagnostics::Breadcrumbs::SetLastPatch("input");
    VantaMP::Game::ReleaseMouseCapture();

    VantaMP::Diagnostics::Breadcrumbs::SetLastPatch("gameplay");
    VantaMP::Game::ApplyGameplayPatches();

    // Unlocks the wider modes. The projection itself is corrected later, once
    // there is a window whose shape can be measured.
    VantaMP::Diagnostics::Breadcrumbs::SetLastPatch("widescreen");
    VantaMP::Game::ApplyWidescreenPatches();

    VantaMP::Diagnostics::Breadcrumbs::SetStage(VantaMP::Diagnostics::Stage::WaitingForFrontend);

    InitFunction::RunAll();
    MH_EnableHook(MH_ALL_HOOKS);
}
