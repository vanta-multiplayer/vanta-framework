#include "breadcrumbs.h"

#include <utils/safe_win32.h>

#include <external/sentry/wrapper.h>

#include <atomic>

namespace VantaMP::Diagnostics {
    namespace {
        // Plain atomics rather than anything with a constructor: these are read
        // from a crash handler, which runs in a process that has already gone
        // wrong. Nothing here may allocate, lock, or depend on static
        // initialisation order.
        std::atomic<Stage> gStage {Stage::Bootstrap};

        // String LITERALS only, never owned strings. The pointer is read during
        // a crash, so it has to remain valid in a process whose heap may be
        // corrupt - which is true of a literal and of nothing else.
        std::atomic<const char *> gLastPatch {"(none)"};
        std::atomic<const char *> gLastHook {"(none)"};
        std::atomic<const char *> gLastNativeCall {"(none)"};
        std::atomic<const char *> gLastOperation {"(none)"};
    } // namespace

    const char *ToString(Stage stage) noexcept {
        switch (stage) {
        case Stage::Bootstrap: return "Bootstrap";
        case Stage::GameIdentified: return "GameIdentified";
        case Stage::PatchesApplying: return "PatchesApplying";
        case Stage::HooksInstalling: return "HooksInstalling";
        case Stage::WaitingForFrontend: return "WaitingForFrontend";
        case Stage::FrontendReady: return "FrontendReady";
        case Stage::StartingGame: return "StartingGame";
        case Stage::WaitingForWorld: return "WaitingForWorld";
        case Stage::WaitingForPlayer: return "WaitingForPlayer";
        case Stage::PlayerReady: return "PlayerReady";
        case Stage::Connecting: return "Connecting";
        case Stage::InGame: return "InGame";
        case Stage::ShuttingDown: return "ShuttingDown";
        }

        return "Unknown";
    }

    namespace Breadcrumbs {
        void SetStage(Stage stage) noexcept {
            // MONOTONIC. Startup callbacks do not fire in stage order -
            // CGame::Init3 completes after the player has been created - so an
            // earlier stage arriving later would erase what actually happened.
            if (stage != Stage::ShuttingDown && stage <= gStage.load(std::memory_order_relaxed)) {
                return;
            }

            gStage.store(stage, std::memory_order_relaxed);

            PublishToCrashReporter();
        }

        Stage CurrentStage() noexcept {
            return gStage.load(std::memory_order_relaxed);
        }

        void SetLastPatch(const char *name) noexcept {
            gLastPatch.store(name, std::memory_order_relaxed);

            // Published immediately, because patches are applied in a burst
            // between two stage changes: waiting for the next stage would mean
            // a crash mid-burst reports the patch before the one that faulted.
            PublishToCrashReporter();
        }

        void SetLastHook(const char *name) noexcept {
            gLastHook.store(name, std::memory_order_relaxed);

            PublishToCrashReporter();
        }

        void SetLastNativeCall(const char *name) noexcept {
            gLastNativeCall.store(name, std::memory_order_relaxed);
        }

        void SetLastOperation(const char *description) noexcept {
            // DELIBERATELY DOES NOT PUBLISH. Unlike patches and hooks, which
            // happen a handful of times at startup, an operation wraps
            // individual calls and can be entered and left constantly. Storing
            // a pointer is free; a Sentry round trip per call is not. The
            // handler reads the value live, so a crash still reports it.
            gLastOperation.store(description, std::memory_order_relaxed);
        }

        const char *LastPatch() noexcept {
            return gLastPatch.load(std::memory_order_relaxed);
        }

        const char *LastHook() noexcept {
            return gLastHook.load(std::memory_order_relaxed);
        }

        const char *LastNativeCall() noexcept {
            return gLastNativeCall.load(std::memory_order_relaxed);
        }

        const char *LastOperation() noexcept {
            return gLastOperation.load(std::memory_order_relaxed);
        }

        void PublishToCrashReporter() noexcept {
            auto &reporter = Framework::External::Sentry::GetCrashReporter();

            if (!reporter.IsValid()) {
                return;
            }

            // ATTACHED AHEAD OF TIME, NOT GATHERED DURING THE CRASH. The
            // handler runs in a broken process; anything it has to build for
            // itself is a chance to fail while reporting a failure. Keeping the
            // context current means a crash only has to send what is already
            // there.
            reporter.SetContext("vanta", {
                                             {"stage", std::string(ToString(CurrentStage()))},
                                             {"last_patch", std::string(LastPatch())},
                                             {"last_hook", std::string(LastHook())},
                                             {"last_native_call", std::string(LastNativeCall())},
                                             {"last_operation", std::string(LastOperation())},
                                         });

            // Also a tag, so reports can be grouped by how far the client got.
            reporter.SetTag("vanta.stage", ToString(CurrentStage()));
        }
    } // namespace Breadcrumbs
} // namespace VantaMP::Diagnostics
