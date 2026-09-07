#pragma once

namespace VantaMP::Version {
    extern const char *gitLong;
    extern const char *git;
    extern const char *rel;

    // GitHub branch this binary was built from (or the channel branch CI
    // stamped with -DVANTAMP_GIT_BRANCH / GITHUB_REF_NAME).
    extern const char *branch;

    // test or release — the platform this build is going out under.
    extern const char *environment;

    // "0.1.0 test (develop)" for logs, crash reports and the console.
    const char *Label();
}; // namespace VantaMP::Version
