#include <Windows.h>

#include <launcher/project.h>

#include "shared/constants.h"

int main(void) {
    Framework::Launcher::ProjectConfiguration config;
    config.destinationDllName = L"VantaMPClient.dll";
    config.executableName     = VantaMP::Shared::kGameExecutable;
    config.name               = "VantaMP";
    config.platform           = Framework::Launcher::ProjectPlatform::ROCKSTAR;
    config.launchType         = Framework::Launcher::ProjectLaunchType::PE_LOADING;
    config.rockstarTitleKey   = VantaMP::Shared::kRockstarTitleKey;

    // The RGL build ships its code encrypted; capture it once from a launcher-authorised run and
    // map the game from that cache afterwards.
    config.useRockstarImageSnapshot = true;

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
