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

        // CWorld::Players, the player-info array as the GAME shipped it. A
        // global, so it is read out of the operand of an instruction that
        // indexes it rather than scanned for directly - .data has no bytes worth
        // matching.
        //
        // NOT WHERE THE PLAYERS ARE, once ExpandPlayerSlots has run. That
        // relocates the array and rewrites every reference to it, so the game
        // writes players somewhere else and this address holds the abandoned
        // original. Read a player through Game::PlayerInfoAt, which knows where
        // the live array is; this field is only for the relocation itself.
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
        // Two candidate sites for the pause-on-focus-loss branch: which one
        // applies depends on the build, so the bytes at the first are inspected
        // at runtime. Getting this wrong NOPs code that was not the check, and
        // the game freezes whenever it loses focus - which alt-tabbing between
        // two clients does constantly.
        uintptr_t CMenuManager__PauseCheckProbe = 0;
        uintptr_t CMenuManager__PauseCheckAlt   = 0;
        uintptr_t CMenuManager__OnFocusLoss       = 0;
        uintptr_t CMenuManager__AfterAltTabFlag   = 0;

        // Pause flags and the game-state word, all globals - read out of the
        // operands of instructions that touch them.
        uintptr_t CTimer__UserPause = 0;
        uintptr_t CMenuManager__Pause = 0;
        uintptr_t gGameState = 0;

        // RsGlobal. Its maximumWidth/maximumHeight (at +4 and +8) are what the
        // engine believes it is rendering at - nearly five hundred sites read
        // them - so they have to agree with the back buffer's actual size.
        uintptr_t RsGlobal = 0;

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

        // --- Input -----------------------------------------------------------
        //
        // GTA takes the mouse through DirectInput in EXCLUSIVE mode and
        // re-centres the pointer every frame for mouse-look. That is right for
        // a single-player game with no interface of its own, and wrong for a
        // client that has to show a menu: the pointer belongs to the game, so
        // the system cursor is dragged to the window centre and pinned there.
        uintptr_t DirectInput__MouseExclusiveMode = 0;
        uintptr_t DirectInput__MouseReinit1       = 0;
        uintptr_t DirectInput__MouseReinit2       = 0;
        uintptr_t DirectInput__MouseReinit3       = 0;
        uintptr_t Cursor__Recentre                = 0;
        uintptr_t Cursor__Hiding                  = 0;

        // The flag the game sets when it has focus. Gates the MOUSE only -
        // keyboard and movement arrive through DirectInput, which does not care
        // which window the user is looking at.
        uintptr_t IsForegroundApp = 0;

        // --- Gameplay the server owns instead ---------------------------------
        //
        // Single-player systems that would otherwise change state the server is
        // authoritative over, announce things about a game nobody is playing,
        // or make the client behave differently to everyone else's.
        uintptr_t CStats__MessageDisplay1    = 0;
        uintptr_t CStats__MessageDisplay2    = 0;
        uintptr_t CStats__Increment          = 0;
        uintptr_t CStats__Decrement1         = 0;
        uintptr_t CStats__Decrement2         = 0;
        uintptr_t CHud__VitalStatsBranch     = 0;
        uintptr_t CPed__HungerMessage        = 0;
        uintptr_t SurfaceInfos__CantSprintOn = 0;
        uintptr_t Weapon__ThrowableAccuracy  = 0;
        uintptr_t CTask__CarJackSelect       = 0;
        uintptr_t CPostEffects__SpeedBlur    = 0;
        uintptr_t CPedStats__HeadingRate     = 0;
        uintptr_t CTimer__FrameDelayJump     = 0;
        uintptr_t CTimer__FrameDelayStackFix = 0;
        uintptr_t CTimer__FrameDelayPopFix   = 0;
        uintptr_t Weapon__StrafeDelta        = 0;
        uintptr_t Weapon__StrafeMultiply     = 0;
        uintptr_t CCamera__DeathFade         = 0;
        uintptr_t CGarages__Update           = 0;
        uintptr_t CCamera__IdleProcess       = 0;
        uintptr_t CMenuManager__SaveSettings = 0;
        uintptr_t CGameLogic__Update         = 0;
        uintptr_t CTrain__ForbiddenCross     = 0;
        uintptr_t CSamSite__Behaviour        = 0;
        uintptr_t CReplay__FinishPlayback    = 0;
        uintptr_t CReplay__TriggerPlayback   = 0;
        uintptr_t CReplay__Update            = 0;
        uintptr_t CReplay__PlayFromHD        = 0;

        // --- Widescreen --------------------------------------------------------
        //
        // The game gates its widescreen modes behind a resolution check and then
        // projects everything for 4:3 regardless, so a 16:9 window gets a
        // stretched image with the HUD inset from the corners.
        uintptr_t Widescreen__Check1      = 0;
        uintptr_t Widescreen__Check2      = 0;
        uintptr_t Widescreen__Nop1        = 0;
        uintptr_t Widescreen__Nop2        = 0;
        uintptr_t Widescreen__Nop3        = 0;
        uintptr_t CDraw__CalculateAspect  = 0;
        uintptr_t CHud__AspectMultiplier  = 0;
        uintptr_t CCamera__CrosshairScale = 0;
        uintptr_t CDraw__AspectRatio = 0;

        // --- Player slots ------------------------------------------------------
        //
        // The game allocates room for two players, which is what its co-op mode
        // needed. These are the four `push imm8` sites that say how many, and
        // the pad array that has to be relocated alongside CWorld::Players.
        uintptr_t Alloc__PlayerInfo1 = 0;
        uintptr_t Alloc__PlayerInfo2 = 0;
        uintptr_t Alloc__Pad1        = 0;
        uintptr_t Alloc__Pad2        = 0;
        uintptr_t CPad__Pads         = 0;

        // --- Remote players ----------------------------------------------------
        //
        // The game processes exactly one player and reads its input, camera and
        // info from fixed globals. Remote peds are driven by the same code under
        // a briefly swapped context, so these are the sites that have to be
        // hooked or saved around.
        uintptr_t CPlayerPed__ProcessControl      = 0;
        uintptr_t CPlayerPed__ProcessControlVtable = 0;
        uintptr_t CPlayerPed__GetPlayerInfo       = 0;
        uintptr_t CPad__GetPad                    = 0;

        // Wiping the local pad, so the game acts on no input for a frame.
        //
        // Needed because DirectInput hands an unfocused process the keyboard:
        // with two clients open, one keypress would otherwise move both.
        uintptr_t CPad__Clear             = 0;
        uintptr_t CPad__ClearMouseHistory = 0;

        // The mouse deltas the camera reads directly, which CPad::Clear does
        // not touch. NewMouseControllerState; the old state follows it.
        uintptr_t CPad__NewMouseState = 0;

        // --- Pose that is a task, not a flag -----------------------------------
        //
        // Writing bIsDucking does nothing lasting: it is an OUTPUT the game
        // recomputes from whatever task is running, so a remote player told to
        // crouch stands straight back up on the next frame. The pose has to be
        // produced the way the owner's game produced it - by running the task.
        //
        // MTA does the same thing for the same reason: CClientPed::Duck creates
        // a TASK_SIMPLE_DUCK rather than setting the flag.
        uintptr_t CTask__operator_new            = 0;
        uintptr_t CTaskSimpleDuck__Constructor   = 0;
        uintptr_t CTaskManager__SetTaskSecondary = 0;
        uintptr_t CTaskManager__GetTaskSecondary = 0;

        // Swimming is the same story as ducking, one level up. Being in water
        // is an OUTPUT too - the flags come from the task, not the other way
        // round - so a remote player who is swimming has to be handed the task
        // that produces them, or they are drawn walking along the riverbed.
        //
        // A PRIMARY task this time, hence SetTask rather than SetTaskSecondary.
        uintptr_t CTaskComplexInWater__Constructor = 0;
        uintptr_t CTaskManager__SetTask            = 0;

        // Camera rotation drives which way "forward" is, and mouse look decides
        // whether local mouse movement steers whoever is being processed.
        uintptr_t CCamera__Rotation    = 0;
        uintptr_t CCamera__MouseLook   = 0;

        // --- Game methods called directly --------------------------------------
        //
        // plugin-sdk is used header-only here, so it DECLARES these but their
        // definitions live at 1.0 US addresses in .cpp files this build does not
        // compile - linking against them fails. Resolving them as patterns keeps
        // the SDK to layouts, which is what it is here for, and keeps the calls
        // build-independent like everything else.
        uintptr_t CPed__operator_new       = 0;
        uintptr_t CPed__operator_delete    = 0;
        uintptr_t CPlayerPed__Constructor  = 0;
        uintptr_t CPed__SetModelIndex      = 0;
        uintptr_t CPlaceable__SetOrientation = 0;
        uintptr_t CPedIntelligence__FlushImmediately = 0;
        uintptr_t CPedClothesDesc__Constructor = 0;
        uintptr_t CPedClothesDesc__Initialise  = 0;
        uintptr_t CWorld__Add    = 0;
        uintptr_t CWorld__Remove = 0;

        // --- Running more than one client --------------------------------------
        //
        // GTA refuses to start twice, and testing a multiplayer platform means
        // running two clients on one machine constantly.
        uintptr_t MultiInstanceCheckCall = 0;
        uintptr_t IsAlreadyRunning       = 0;
        uintptr_t CdStreamSemaphoreName  = 0;

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
