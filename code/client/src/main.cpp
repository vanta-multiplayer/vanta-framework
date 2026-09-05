// Include safe_win32 FIRST to avoid winsock conflicts
#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>
#include <utils/string_utils.h>

#include "core/globals.h"
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

    VantaMP::SDK::Patterns::InitPatterns();

    InitFunction::RunAll();
    MH_EnableHook(MH_ALL_HOOKS);
}
