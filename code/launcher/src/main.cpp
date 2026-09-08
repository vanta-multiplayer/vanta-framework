#include <Windows.h>

#include <launcher/project.h>

#include "eac_launch.h"
#include "game_image.h"
#include "shared/constants.h"

int main(void) {
    // Before Framework::Project captures cwd as the client runtime path, pin to
    // the launcher exe directory. EOSBootstrapper often starts us with cwd under
    // EasyAntiCheat\, which would otherwise break DLL/config/crashpad paths.
    (void)VantaMP::Launcher::PinWorkingDirectoryToLauncher();

    Framework::Launcher::ProjectConfiguration config;
    config.destinationDllName = L"VantaMPClient.dll";
    config.executableName     = VantaMP::Shared::kGameExecutable;
    config.name               = "VantaMP";
    config.platform           = Framework::Launcher::ProjectPlatform::ROCKSTAR;
    config.launchType         = Framework::Launcher::ProjectLaunchType::PE_LOADING;
    config.rockstarTitleKey   = VantaMP::Shared::kRockstarTitleKey;

    // The RGL build ships its code encrypted, so it is captured once from a
    // launcher-authorised run and the game is mapped from that cache afterwards.
    //
    // Asked rather than assumed: Vanta supports the retail and Steam releases
    // too, and neither is wrapped. Requesting a snapshot for one of those fails
    // with "make sure the Rockstar Games Launcher is installed", which is not
    // advice a player who owns the game elsewhere can act on.
    config.useRockstarImageSnapshot = VantaMP::Launcher::GameNeedsImageSnapshot();

    config.allowManualGamePathFallback = true;
    config.promptTitle                 = "Select your GTA: San Andreas executable (gta_sa.exe)";
    config.promptFilter                = "gta_sa.exe";
    config.promptFilterName            = "gta_sa.exe";

#ifdef FW_DEBUG
    config.allocateDeveloperConsole = true;
    config.developerConsoleTitle    = L"vantamp: dev-console";
#endif

    Framework::Launcher::Project project(config);

    // Re-enter through start_protected_game.exe when needed. That Anti-Cheat
    // bootstrapper starts this launcher again; PE-mapping GTA then happens in
    // the protected process (same process as VantaMPClient + EOS).
    if (!VantaMP::Launcher::EnsureEacProtectedProcess()) {
        return 0;
    }

    if (!project.Launch()) {
        return 1;
    }
    return 0;
}
