#include <Windows.h>

#include <launcher/project.h>

#include "game_image.h"
#include "shared/constants.h"

int main(void) {
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
    if (!project.Launch()) {
        return 1;
    }
    return 0;
}
