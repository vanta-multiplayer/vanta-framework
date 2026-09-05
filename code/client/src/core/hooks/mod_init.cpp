#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <utils/hooking/hook_function.h>

#include "core/globals.h"
#include "game/player_slots.h"
#include "sdk/patterns.h"
#include "shared/version.h"

namespace {
    // The game installs its own unhandled filter, so a fault never reaches ours
    LONG CALLBACK ReportFirstChanceFault(PEXCEPTION_POINTERS info) {
        const auto code = info->ExceptionRecord->ExceptionCode;
        if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_ILLEGAL_INSTRUCTION) {
            const auto logger = Framework::Logging::GetLogger("Fault");
            logger->error("First-chance {:08X} at {} (eip {:08X}, esp {:08X})", code, info->ExceptionRecord->ExceptionAddress, info->ContextRecord->Eip, info->ContextRecord->Esp);
            logger->flush();
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
} // namespace

static InitFunction init(
    []() {
        const auto logger = Framework::Logging::GetLogger("ModInit");
        logger->info("VantaMP client {} attached, project path {}", VantaMP::Version::rel, VantaMP::Core::gProjectPath);
        logger->info("Players array relocated to {}", VantaMP::Game::PlayerInfoAt(0));

        AddVectoredExceptionHandler(1, ReportFirstChanceFault);
    },
    "ModInit");
