#pragma once

#include <cstdint>

namespace VantaMP::Diagnostics {
    // How far the client had got when something went wrong.
    //
    // A crash address alone rarely identifies the real fault when a mod is
    // driving a 2005 game through an initialisation order it was never designed
    // for. Knowing the fault happened during WaitingForPlayer rather than
    // Running immediately separates "we touched a game system too early" from
    // "we corrupted something later" - and those have completely different
    // suspects.
    enum class Stage : std::uint8_t {
        Bootstrap,
        GameIdentified,
        PatchesApplying,
        HooksInstalling,

        // Patches are in; waiting for the game to reach its frontend.
        WaitingForFrontend,

        // The frontend has been reached and taken over.
        FrontendReady,

        // CGame::Init3 is running.
        StartingGame,

        // Engine initialised; waiting for the world to become usable.
        WaitingForWorld,

        // Waiting for the point at which the local player can be created.
        WaitingForPlayer,

        // The local player exists.
        PlayerReady,

        Connecting,
        InGame,
        ShuttingDown,
    };

    [[nodiscard]] const char *ToString(Stage stage) noexcept;

    // Recorded as the client works, so a crash report can say what was being
    // attempted rather than only where the instruction pointer landed.
    //
    // DELIBERATELY GLOBAL AND LOCK-FREE. A crash handler runs in a context where
    // taking a lock may deadlock, so reads must never block - and a write has to
    // be cheap enough to put around every dangerous call without thinking about
    // it.
    namespace Breadcrumbs {
        // MONOTONIC, because startup callbacks do not fire in stage order:
        // CGame::Init3 completes after the player has spawned, so an earlier
        // stage arriving later would erase the truth. ShuttingDown is the one
        // exception, since teardown genuinely follows everything.
        void SetStage(Stage stage) noexcept;
        [[nodiscard]] Stage CurrentStage() noexcept;

        // The last patch applied, hook installed, or game function called into.
        // The last is set immediately before any call that could fault and
        // cleared after it returns.
        void SetLastPatch(const char *name) noexcept;
        void SetLastHook(const char *name) noexcept;
        void SetLastNativeCall(const char *name) noexcept;
        void SetLastOperation(const char *description) noexcept;

        [[nodiscard]] const char *LastPatch() noexcept;
        [[nodiscard]] const char *LastHook() noexcept;
        [[nodiscard]] const char *LastNativeCall() noexcept;
        [[nodiscard]] const char *LastOperation() noexcept;

        // Hands everything above to the framework's crash reporter, so a report
        // carries it. Called once the reporter exists, and again whenever the
        // stage changes - a crash uses whatever was last attached.
        void PublishToCrashReporter() noexcept;
    } // namespace Breadcrumbs

    // Records what is being attempted for the rest of a scope, and clears it
    // afterwards so a breadcrumb cannot be left stale once the risky call
    // succeeded.
    class ScopedOperation final {
      public:
        explicit ScopedOperation(const char *description) noexcept {
            Breadcrumbs::SetLastOperation(description);
        }

        ~ScopedOperation() {
            Breadcrumbs::SetLastOperation("(none)");
        }

        ScopedOperation(const ScopedOperation &)            = delete;
        ScopedOperation &operator=(const ScopedOperation &) = delete;
    };
} // namespace VantaMP::Diagnostics

#define VANTAMP_OPERATION(description) ::VantaMP::Diagnostics::ScopedOperation vantamp_operation_##__LINE__(description)
