#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <utils/hooking/hook_function.h>

#include "core/diagnostics/breadcrumbs.h"
#include "core/diagnostics/crash_report.h"
#include "core/globals.h"
#include "game/entities/player_slots.h"
#include "sdk/patterns.h"
#include "shared/version.h"

namespace {
    // The game installs its own unhandled filter, so a fault never reaches ours
    LONG CALLBACK ReportFirstChanceFault(PEXCEPTION_POINTERS info) {
        const auto code = info->ExceptionRecord->ExceptionCode;
        if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_ILLEGAL_INSTRUCTION) {
            const auto logger = Framework::Logging::GetLogger("Fault");
            const auto address = info->ExceptionRecord->ExceptionAddress;

            HMODULE module = nullptr;
            char moduleName[MAX_PATH] {};
            const bool haveModule = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &module) != FALSE;

            if (haveModule && GetModuleFileNameA(module, moduleName, MAX_PATH)) {
                logger->error(
                    "First-chance {:08X} at {} (eip {:08X}, esp {:08X}, module {}+{:08X})",
                    code,
                    address,
                    info->ContextRecord->Eip,
                    info->ContextRecord->Esp,
                    moduleName,
                    static_cast<unsigned>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module)));
            }
            else {
                logger->error("First-chance {:08X} at {} (eip {:08X}, esp {:08X})", code, address, info->ContextRecord->Eip, info->ContextRecord->Esp);
            }

            logger->flush();
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
} // namespace

static InitFunction init(
    []() {
        AddVectoredExceptionHandler(1, ReportFirstChanceFault);

        // EVERY BREADCRUMB SO FAR WAS RECORDED WITHOUT A REPORTER TO SEND IT
        // TO. Patching and hooking run in InitClient, before the framework
        // instance exists, so SetStage found no reporter and published nothing -
        // and the patch window is exactly where a crash is most likely and
        // least self-explanatory. This is the first moment the reporter is
        // available, so hand it the whole backlog at once.
        VantaMP::Diagnostics::Breadcrumbs::PublishToCrashReporter();

        // RE-INSTALLED, because sentry_init put crashpad's own unhandled-
        // exception filter on top of ours: crashpad captured the fault and
        // ended the process, so our handler never ran and no local report was
        // written. Installing again now puts ours back in front, and it chains
        // to whatever it displaced - so a crash produces the local report AND
        // the Sentry event, rather than one silently replacing the other.
        VantaMP::Diagnostics::CrashReport::Install(VantaMP::Core::gProjectPath + "/crashes");
    },
    "ModInit");
