// Include safe_win32 FIRST to avoid winsock conflicts
#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>
#include <utils/string_utils.h>

#include "core/globals.h"
#include "game/boot.h"
#include "game/population.h"
#include "sdk/game_build.h"
#include "sdk/patterns.h"

namespace VantaMP::Core {
    std::string gProjectPath;
} // namespace VantaMP::Core

extern "C" void __declspec(dllexport) InitClient(const wchar_t *projectPath) {
    VantaMP::Core::gProjectPath = Framework::Utils::StringUtils::WideToNormal(projectPath);

    Framework::Logging::GetInstance()->SetLogName("VantaMP");
    Framework::Logging::GetInstance()->SetLogFolder(fmt::format("{}/logs", VantaMP::Core::gProjectPath));

    MH_Initialize();
    hook::set_base();

    // Which build we are in. Recorded rather than enforced: the scans below
    // decide whether this executable is usable, and they say so loudly if it is
    // not. Knowing the build first only makes that failure legible.
    (void)VantaMP::SDK::IdentifyGameBuild();

    VantaMP::SDK::Patterns::InitPatterns();

    // Before the game gets a chance to populate the world. GTA starts inventing
    // pedestrians and traffic as soon as it streams anything in, and a ped that
    // already exists has to be deleted rather than never created - which fights
    // the very lifetime rules this is establishing.
    VantaMP::Game::DisableAutonomousPopulation();

    // Everything above runs before the game has drawn a frame, which is what
    // makes bypassing its frontend possible at all: these rewrite call sites
    // the game has not reached yet.
    VantaMP::Game::ApplyBootPatches();

    InitFunction::RunAll();
    MH_EnableHook(MH_ALL_HOOKS);
}
