#pragma once

#include <filesystem>

namespace VantaMP::Diagnostics {
    // Writes a human-readable crash report and a minidump beside it.
    //
    // WHY THIS EXISTS ALONGSIDE THE FRAMEWORK'S SENTRY SUPPORT. Sentry only
    // comes up when a DSN is configured, and it reports to a service. This is
    // local, needs no account and no network, and is what makes a crash on a
    // developer's own machine explainable within seconds - which is the case
    // that actually happens while a game mod is being built.
    //
    // The two are not alternatives: breadcrumbs feed both, so configuring a DSN
    // later adds aggregation without changing anything here.
    namespace CrashReport {
        // Installs the unhandled-exception filter. Reports land in `directory`,
        // named by local time, as a .log next to a .dmp.
        void Install(const std::filesystem::path &directory);

        void Uninstall();
    } // namespace CrashReport
} // namespace VantaMP::Diagnostics
