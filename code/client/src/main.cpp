#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>
#include <utils/string_utils.h>

#include "core/client_config.h"
#include "core/diagnostics/crash_report.h"
#include "core/globals.h"
#include "eac/host_modules.h"
#include "game/startup.h"

namespace VantaMP::Core {
    std::string gProjectPath;
} // namespace VantaMP::Core

extern "C" void __declspec(dllexport) InitClient(const wchar_t *projectPath) {
    VantaMP::Core::gProjectPath = Framework::Utils::StringUtils::WideToNormal(projectPath);
    Framework::Logging::GetInstance()->SetLogName(fmt::format("VantaMP-{}", GetCurrentProcessId()));
    Framework::Logging::GetInstance()->SetLogFolder(fmt::format("{}/logs", VantaMP::Core::gProjectPath));
    VantaMP::Diagnostics::CrashReport::Install(fmt::format("{}/crashes", VantaMP::Core::gProjectPath));
    VantaMP::Core::LoadClientConfig(VantaMP::Core::gProjectPath);

    MH_Initialize();
    // Host stubs (eax/ogg/vorbis) ship beside the launcher; no LoadLibrary hooks.
    VantaMP::Eac::InstallHostModuleRedirects();

    hook::set_base();
    VantaMP::Game::Start();
    InitFunction::RunAll();
    MH_EnableHook(MH_ALL_HOOKS);
}
