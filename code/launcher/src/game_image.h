#pragma once

namespace VantaMP::Launcher {
    // Does the game this launcher will start need its code captured first?
    //
    // The Rockstar Games Launcher build of San Andreas is wrapped by MTLX.dll:
    // its code section is encrypted on disk and only decrypted for a process
    // the Rockstar Games Launcher itself started. Such a build cannot be mapped
    // from the file, which is why the framework can capture a snapshot of the
    // decrypted code on a first, launcher-authorised run.
    //
    // A retail or Steam copy needs none of that, and asking for it anyway fails
    // with "make sure the Rockstar Games Launcher is installed" - advice the
    // player cannot act on, because it is not the game they own. Vanta supports
    // several builds, so this is decided rather than assumed.
    //
    // Answered from the executable's import table: a wrapped build imports the
    // wrapper, an unwrapped one does not. That holds for builds nobody has
    // catalogued yet, which a file size or hash would not.
    //
    // Errs towards TRUE. If the game cannot be found or read here, the
    // framework's own resolution has not run yet and may still succeed - so the
    // snapshot stays enabled and the decision is left to the path that has the
    // real answer.
    [[nodiscard]] bool GameNeedsImageSnapshot();
} // namespace VantaMP::Launcher
