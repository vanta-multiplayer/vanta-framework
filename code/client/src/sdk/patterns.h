#pragma once

#include <cstdint>

namespace VantaMP::SDK {
    // Every game address the client uses, resolved by signature scan at startup.
    //
    // WHY SCANS AND NOT A TABLE OF ADDRESSES. Vanta's previous client held ~160
    // literal addresses for GTA:SA 1.0 US. They were correct, and they were
    // correct for exactly one build: the Rockstar Games Launcher release shares
    // none of them, and a table pointed at the wrong build corrupts memory at
    // effectively random locations rather than failing.
    //
    // A signature describes what the code DOES, so it finds its own way around
    // whichever build is loaded. One binary can then serve several builds,
    // which is the whole reason for the change.
    //
    // The old table was not wasted: each address in it was the ground truth a
    // signature was derived and verified against, by scripts/derive_pattern.py.
    // Where a comment below names a 1.0 US address, that is its provenance -
    // the address the signature was proven to resolve to - and not a value the
    // client uses.
    //
    // ONCE RESOLVED, EVERY ENTRY IS VALID. hook::get_pattern asserts on exactly
    // one match, so a signature that no longer matches, or that became
    // ambiguous, fails here at startup rather than somewhere later. Nothing
    // downstream tests these for zero; there is no state in which they are.
    class Patterns final {
      public:
        // --- World -----------------------------------------------------------

        // CWorld::Players, the player-info array. A global, so it is read out of
        // the operand of an instruction that indexes it rather than scanned for
        // directly - .data has no bytes worth matching.
        uintptr_t CWorld__Players = 0;

        // --- Boot: loading screens and the frontend ---------------------------
        //
        // GTA boots into a single-player menu and a sequence of loading screens.
        // A multiplayer client replaces both with its own connect flow, so these
        // are the sites that draw them, drive their state machine, or read their
        // input.
        uintptr_t CGame__Init3Call           = 0;
        uintptr_t CLoadingScreen__Draw       = 0;
        uintptr_t CLoadingScreen__NewChunk   = 0;
        uintptr_t CLoadingScreen__DrawCall1  = 0;
        uintptr_t CLoadingScreen__DrawCall2  = 0;
        uintptr_t CLoadingScreen__DrawReturn = 0;
        uintptr_t CLoadingScreen__AudioCall  = 0;
        uintptr_t CGame__StateResetToZero    = 0;

        uintptr_t CMenuManager__ProcessCall       = 0;
        uintptr_t CMenuManager__DrawStandardMenus = 0;
        uintptr_t CMenuManager__InputCall         = 0;
        uintptr_t CMenuManager__BackgroundRect    = 0;
        uintptr_t CMenuManager__BackgroundTexture = 0;
        uintptr_t CMenuManager__SetRenderStates   = 0;
        uintptr_t CMenuManager__EscapeHandler1    = 0;
        uintptr_t CMenuManager__EscapeHandler2    = 0;
        uintptr_t CMenuManager__PauseCheck        = 0;
        uintptr_t CMenuManager__OnFocusLoss       = 0;
        uintptr_t CMenuManager__AfterAltTabFlag   = 0;

        // Pause flags and the game-state word, all globals - read out of the
        // operands of instructions that touch them.
        uintptr_t CTimer__UserPause = 0;
        uintptr_t CMenuManager__Pause = 0;
        uintptr_t gGameState = 0;

        // Console chatter the game prints at startup. Silenced; it is not ours
        // and it is not useful.
        uintptr_t Print__AspectRatio  = 0;
        uintptr_t Print__SoundManager = 0;

        // --- Script and the local player --------------------------------------
        //
        // GTA's world is driven by a compiled mission script (the SCM). In
        // multiplayer nothing about that script applies - it would start
        // missions, move the player and fight the server for control - so it is
        // not loaded and its interpreter is stubbed out.
        //
        // Its per-frame call is reused: the point where scripts would have
        // begun running is exactly the point at which the world is ready and a
        // player can be created.
        uintptr_t CRunningScript__ProcessOneCommand = 0;
        uintptr_t CRunningScript__OpcodeTable       = 0;
        uintptr_t CRunningScript__ProcessCall       = 0;
        uintptr_t ScmLoadJump                       = 0;
        uintptr_t ScmLoadSkipTarget                 = 0;

        uintptr_t CPlayerPed__SetupPlayerPed = 0;
        uintptr_t CPlayerPed__ModelIdPush    = 0;
        uintptr_t FindPlayerPed              = 0;
        uintptr_t CStreaming__LoadScene      = 0;
        uintptr_t CPlaceable__SetPosn        = 0;
        uintptr_t CWorld__PlayerInFocus      = 0;

        // The camera's fade, used to hide the world until the server has said
        // where the player belongs in it.
        uintptr_t CCamera__Fade          = 0;
        uintptr_t CCamera__SetFadeColour = 0;
        uintptr_t TheCamera              = 0;

        // --- Population ------------------------------------------------------
        //
        // GTA's autonomous population is off in multiplayer: the server decides
        // which peds and vehicles exist. These are the sites that create them,
        // remove them, or stream models on their behalf.
        uintptr_t CPopulation__Add               = 0;
        uintptr_t CPopulation__ManagePed         = 0;
        uintptr_t CPopulation__RemovePed         = 0;
        uintptr_t CPopulation__UpdateEarlyOut    = 0;
        uintptr_t CCarCtrl__GenerateRandomCars   = 0;
        uintptr_t CCarCtrl__RemoveCarsIfPoolFull = 0;
        uintptr_t CStreaming__StreamPedsAndCars  = 0;
        uintptr_t CStreaming__AmbientModelsJump  = 0;
        uintptr_t CGangWars__Update              = 0;
        uintptr_t CPedType__InterestingEvents    = 0;
        uintptr_t CPed__PlayerConversations      = 0;

        // Resolves everything above. Called once, before anything reads a field.
        static void InitPatterns();
    };

    extern Patterns gPatterns;
} // namespace VantaMP::SDK
