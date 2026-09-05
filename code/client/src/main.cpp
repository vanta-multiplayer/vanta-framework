// Include safe_win32 FIRST to avoid winsock conflicts
#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>
#include <utils/string_utils.h>

#include "core/client_config.h"
#include "core/globals.h"
#include "game/boot.h"
#include "game/display.h"
#include "game/gameplay.h"
#include "game/input.h"
#include "game/player_slots.h"
#include "game/population.h"
#include "game/world.h"
#include "sdk/game_build.h"
#include "sdk/patterns.h"

namespace VantaMP::Core {
    std::string gProjectPath;
} // namespace VantaMP::Core

extern "C" void __declspec(dllexport) InitClient(const wchar_t *projectPath) {
    VantaMP::Core::gProjectPath = Framework::Utils::StringUtils::WideToNormal(projectPath);

    Framework::Logging::GetInstance()->SetLogName("VantaMP");
    Framework::Logging::GetInstance()->SetLogFolder(fmt::format("{}/logs", VantaMP::Core::gProjectPath));

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
    (void)VantaMP::SDK::IdentifyGameBuild();

    VantaMP::SDK::Patterns::InitPatterns();

    // FIRST OF THE WORLD PATCHES, because it rewrites the code every later
    // patch is written into. Running it afterwards would scan over bytes Vanta
    // had already changed, and any of those that happened to look like an array
    // reference would be rewritten a second time.
    VantaMP::Game::ExpandPlayerSlots();

    // Before the game gets a chance to populate the world. GTA starts inventing
    // pedestrians and traffic as soon as it streams anything in, and a ped that
    // already exists has to be deleted rather than never created - which fights
    // the very lifetime rules this is establishing.
    VantaMP::Game::DisableAutonomousPopulation();

    // Everything above runs before the game has drawn a frame, which is what
    // makes bypassing its frontend possible at all: these rewrite call sites
    // the game has not reached yet.
    VantaMP::Game::ApplyBootPatches();

    // Last of the startup patches: this one disables the mission script and
    // takes over its per-frame call, which is where the local player is
    // created once the world is complete.
    VantaMP::Game::ApplyScriptPatches();

    // Has to land before the game acquires the mouse, which it does during its
    // own startup - from a frame callback this would already be too late.
    VantaMP::Game::ReleaseMouseCapture();

    VantaMP::Game::ApplyGameplayPatches();

    // Unlocks the wider modes. The projection itself is corrected later, once
    // there is a window whose shape can be measured.
    VantaMP::Game::ApplyWidescreenPatches();

    InitFunction::RunAll();
    MH_EnableHook(MH_ALL_HOOKS);
}
