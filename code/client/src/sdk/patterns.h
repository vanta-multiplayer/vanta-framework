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

        // Server-authoritative clock and timer scale. Game speed is rewritten
        // every client pulse (MTA-style) so a local poke cannot keep the sim
        // accelerated. Weather is ForceWeatherNow, not a gradual blend.
        uintptr_t CTimer__ms_fTimeScale                    = 0;
        uintptr_t CClock__ms_nMillisecondsPerGameMinute    = 0;
        uintptr_t CClock__ms_nGameClockHours               = 0;
        uintptr_t CClock__ms_nGameClockMinutes             = 0;
        uintptr_t CWeather__ForceWeatherNow                = 0;
        uintptr_t CGame__ms_fGravity                       = 0;
        uintptr_t CRenderer__ms_fFarClipPlane              = 0;
        uintptr_t CRenderer__ms_fFogStart                  = 0;
        uintptr_t CWeather__Rain                           = 0;
        uintptr_t CWater__ms_fWaveHeight                   = 0;
        uintptr_t CTimeCycle__m_nSkyTopRed                 = 0;
        uintptr_t CTimeCycle__m_nSunCoreRed                = 0;
        uintptr_t CTimeCycle__m_fSunSize                   = 0;
        uintptr_t CCoronas__MoonSize                       = 0;
        uintptr_t CTimeCycle__m_fWaterRed                  = 0;
        uintptr_t CWater__UnderwaterRGBA                   = 0;
        uintptr_t CPostEffects__HeatHazeHourStart          = 0;
        uintptr_t CPostEffects__HeatHazeRandomShift        = 0;
        uintptr_t CPostEffects__HeatHazeScanSizeX          = 0;
        uintptr_t CPostEffects__bHeatHazeFX                = 0;
        uintptr_t CPostEffects__HeatHazeAutoType           = 0;
        uintptr_t CClouds__VolumetricRender                = 0;
        uintptr_t CClouds__BottomRender                    = 0;
        uintptr_t CClouds__SkylineRender                   = 0;
        uintptr_t CPlane__TrailRender                      = 0;
        uintptr_t CWorld__fJetpackMaxHeight                = 0;
        uintptr_t CWeather__WindVel                        = 0;

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
        // begun running is exactly the point at which the world is ready and
        // the local GTA ped must exist (the camera registers a reference to it).
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

        // Pushes a written CMatrix out to the RwMatrix the renderer draws
        // from, and then to the frame hierarchy. A pose write that skips
        // these moves the collision but not (reliably) the mesh.
        uintptr_t CEntity__UpdateRwMatrix    = 0;
        uintptr_t CEntity__UpdateRwFrame     = 0;
        uintptr_t CWorld__PlayerInFocus      = 0;

        // The camera's fade, used to hide the world until the server has said
        // where the player belongs in it.
        uintptr_t CCamera__Fade          = 0;
        uintptr_t CCamera__SetFadeColour = 0;
        uintptr_t TheCamera              = 0;

        // CameraSize (0x72FC70) rebuilds the RwCamera frame/Z rasters from
        // RsGlobal. After a D3D Reset those rasters still wrap the old
        // surfaces, which is the black leftover on a live resize.
        // RwCameraClear (0x7EE340) wipes the new rasters so one uncleared
        // frame does not flash.
        uintptr_t CameraSize    = 0;
        uintptr_t RwCameraClear = 0;

        // CGame::Process (0x53BEE0). Live display apply runs here, not inside
        // BeginScene/Present: Reset from a D3D method is D3DERR_INVALIDCALL
        // even after the previous Present has returned.
        uintptr_t CGame__Process = 0;

        // gta-reversed D3D9DeviceRestoreVideoMemory / SetVideoMode (0x745C70):
        // Release (0x7F7F70), Reset, then restore in this order. Skipping the
        // raster-list restore left post-FX drawing through freed surfaces.
        uintptr_t RwD3D9ReleaseVideoMemory = 0;
        uintptr_t RwD3D9RestoreRasterList  = 0;
        uintptr_t RwD3D9RestoreAfterReset1 = 0;
        uintptr_t RwD3D9RestoreAfterReset2 = 0;
        uintptr_t RwD3D9RestoreAfterReset3 = 0;
        uintptr_t RwD3D9RestoreAfterReset4 = 0;
        uintptr_t CPostEffects__SetupBackBufferVertex = 0;
        uintptr_t CPostEffects__pRasterFrontBuffer    = 0;
        uintptr_t RwRasterDestroy                     = 0;

        // RW's D3DPRESENT_PARAMETERS (0xC9C040). ChangeVideoMode Resets from
        // this block; a live apply has to write the new size here too or the
        // next GTA Reset puts the old back buffer back.
        uintptr_t D3DPresentParameters = 0;

        // RwD3D9RenderSurface (0xC97C30). Depth stencil sits immediately
        // before it at 0xC97C2C. Reset destroys those surfaces; the next
        // GTA present uses the stale pointers unless they are rebound.
        uintptr_t RwD3D9RenderSurface = 0;

        // --- First-run Device Selection --------------------------------------
        //
        // psSelectDevice (0x746190) shows GTA's GPU + mode dialog only when
        // there is more than one D3D adapter. The mid-function site MTA
        // hooks (0x746219) is the compare that decides that. We land there
        // so first setup can force the dialog on a single GPU, and every
        // later launch can skip it.
        uintptr_t PsSelectDeviceHook     = 0;
        uintptr_t VidModeDialogFuncPush  = 0;
        uintptr_t SavedVideoMode         = 0;
        uintptr_t GcurSelVM              = 0;
        uintptr_t RwEngineGetVideoModeInfo = 0;
        uintptr_t RwEngineGetNumVideoModes = 0;

        // SetVideoMode (0x745C70). What the pause-menu display settings
        // call when the player confirms a resolution — not RwD3D9ChangeVideoMode
        // itself. That function does the Reset, the post-FX rebuild and
        // RsGlobal. Calling anything underneath it is how the last live
        // resize crashed.
        uintptr_t SetVideoMode = 0;

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
        uintptr_t CMenuManager__LoadSettings = 0;

        // FrontEndMenuManager. SFX is +0x4F, radio +0x50, both 0-64.
        uintptr_t CMenuManager = 0;

        // AudioEngine singleton and the two master-volume thiscalls.
        uintptr_t CAudioEngine                      = 0;
        uintptr_t CAudioEngine__SetMusicMasterVolume = 0;
        uintptr_t CAudioEngine__SetEffectsMasterVolume = 0;

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

        // The CALL to CPad::UpdatePads inside CGame::Process. After that
        // call the pad is full; we clear it there when the menu owns the
        // keyboard, because EndScene runs after ProcessControl and a clear
        // there is overwritten before the next walk.
        uintptr_t CPad__UpdatePadsCall = 0;

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
        uintptr_t CTaskSimpleJetPack__Constructor = 0;
        uintptr_t CPedIntelligence__GetTaskJetPack = 0;
        uintptr_t CTaskManager__GetSimplestActiveTask = 0;
        uintptr_t CTaskManager__SetTaskSecondary = 0;
        uintptr_t CTaskManager__GetTaskSecondary = 0;

        // Reload is the same story as duck: WEAPONSTATE_RELOADING is an
        // OUTPUT of CTaskSimpleUseGun (command 1). Writing the state alone
        // leaves a remote ped idle with an empty clip. MTA sets the state
        // and lets the existing UseGun task consume it; we also create the
        // task when they are not already aiming, so an empty-clip reload
        // after hip-fire still plays.
        uintptr_t CTaskSimpleUseGun__Constructor = 0;
        uintptr_t CTaskSimpleUseGun__ControlGun  = 0;
        uintptr_t CPedIntelligence__GetTaskUseGun = 0;

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

        // CPlayerPed::SetInitialState. MTA's spawnPlayer calls this to take a
        // dead ped back to standing: death tasks, the DIE/DEAD state and the
        // die-anim flag all go. Writing health alone leaves them on the ground.
        uintptr_t CPlayerPed__SetInitialState = 0;
        uintptr_t CPedClothesDesc__Constructor = 0;
        uintptr_t CPedClothesDesc__Initialise  = 0;
        uintptr_t CWorld__Add    = 0;
        uintptr_t CWorld__Remove = 0;

        // A HELD WEAPON IS A MODEL, NOT A NUMBER. Writing the type into
        // m_aWeapons would describe an armed ped without arming one: the game
        // loads the weapon model, attaches it to the hand bone and picks the
        // animation group from it, none of which a field write does. So the
        // game's own GiveWeapon runs, and SetCurrentWeapon draws it.
        //
        // ClearWeapon is what detaches the model again, which matters because a
        // remote player switching weapons would otherwise accumulate them in
        // both hands.
        uintptr_t CPed__GiveWeapon       = 0;
        uintptr_t CPed__SetCurrentWeapon = 0;
        uintptr_t CPed__ClearWeapon      = 0;

        // Rebuilds the walk anim from m_nAnimGroup. Writing the group
        // alone leaves them on the old clip until the next state change.
        uintptr_t CPed__SetMoveAnim = 0;

        // Clump plugin alpha. GTA rewrites it from ProcessControl, so
        // the caller has to set it every frame.
        uintptr_t CVisibilityPlugins__SetClumpAlpha = 0;

        // --- Streaming ----------------------------------------------------------
        //
        // MODELS HAVE TO BE IN MEMORY BEFORE ANYTHING IS TOLD TO WEAR ONE.
        // Arming a ped with a model the streamer has not loaded does not fail
        // where it was asked: the weapon is recorded, and the game faults a
        // second later when it goes to draw an object that is not there.
        //
        // RequestModel queues, LoadAllRequestedModels blocks until the queue is
        // empty, and HasModelLoaded is how to avoid doing either when the model
        // is already resident - which is the common case, and the one that must
        // not stall the frame.
        uintptr_t CStreaming__RequestModel          = 0;
        uintptr_t CStreaming__LoadAllRequestedModels = 0;
        uintptr_t CStreaming__HasModelLoaded        = 0;

        // Which GTA interior the local player is in. currArea is a global
        // (CGame::currArea); RemoveBuildingsNotInArea is what actually
        // unloads the other area's buildings. Both are what MTA's
        // CWorldSA::SetCurrentArea writes / calls. An area-code write on
        // the ped alone leaves the street loaded over a shop.
        uintptr_t CGame__CurrArea                         = 0;
        uintptr_t CStreaming__RemoveBuildingsNotInArea    = 0;

        // Which model a weapon is worn as. A table lookup in the exe rather than
        // one repeated here, since it also depends on skill level.
        uintptr_t CWeaponInfo__GetWeaponInfo = 0;

        // --- Where a ped points its gun -----------------------------------------
        //
        // WHICH WAY A PED AIMS IS COMPUTED FROM THE LOCAL CAMERA, for every ped,
        // because the game only ever had one player. The horizontal part follows
        // the camera rotation we already swap per remote ped, but the PITCH does
        // not - so without this a remote player aims level whatever their owner
        // is looking at, and their shots leave along the wrong line.
        //
        // Reached through its three call sites rather than by overwriting its
        // prologue. MTA rewrites the first instructions and jumps back in past
        // them, which it has to because it hooks a lot of these; with three
        // sites there is no reason to take the function apart.
        uintptr_t CPedIK__PointGunInDirection = 0;

        uintptr_t CPedIK__PointGunInDirectionCallA = 0;
        uintptr_t CPedIK__PointGunInDirectionCallB = 0;
        uintptr_t CPedIK__PointGunInDirectionCallC = 0;

        // A shot leaving the gun. Hooked so the local player's Fire is reported
        // as its own packet - origin and target are an event, not a field on
        // the 10 Hz snapshot - and a remote player's Fire from input replay is
        // swallowed, because that shot aims with the local camera.
        uintptr_t CWeapon__Fire = 0;

        // The hitscan itself. CWeapon::Fire is the dispatcher; this is what
        // actually draws the tracer. Remotes call it with the owner's line so
        // the shot does not pick up this client's camera.
        uintptr_t CWeapon__FireInstantHit = 0;

        // MTA's two FireInstantHit call-site hooks. The LOS call is where the
        // real start/end live (and where a remote replay overwrites them). The
        // IsPlayer call is what would otherwise send a remote CPlayerPed down
        // the local-camera path.
        uintptr_t CWeapon__FireInstantHit_LosCall     = 0;
        uintptr_t CWeapon__FireInstantHit_IsPlayerCall = 0;
        uintptr_t CWorld__ProcessLineOfSight           = 0;
        uintptr_t CPed__IsPlayer                       = 0;

        // The crosshair ray. Camera.m_vecFront from the muzzle is the follow
        // cam looking down at the ped, which is why remotes saw shots into
        // the dirt. This is what FireInstantHit itself uses when aiming.
        uintptr_t CCamera__Find3rdPersonCamTargetVector = 0;

        // CWorld::pIgnoreEntity. FireBullet sets the shooter here so the
        // hitscan cannot hit the ped that just fired.
        uintptr_t CWorld__pIgnoreEntity = 0;

        // Remote muzzle presentation. Forcing IsPlayer false (so a remote
        // CPlayerPed does not steal this client's camera) also skips the
        // player-only branch that calls DoGunFlash. MTA then does the flash
        // itself: the weapon-mesh flash, the particle, a shell, a point
        // light, and the fire audio event.
        uintptr_t CPed__DoGunFlash                      = 0;
        uintptr_t CPed__GetTransformedBonePosition      = 0;
        uintptr_t Fx_c__TriggerGunshot                  = 0;
        uintptr_t CWeapon__AddGunshell                  = 0;
        uintptr_t CAEPedWeaponAudioEntity__AddAudioEvent = 0;
        uintptr_t CPointLights__AddLight                = 0;
        uintptr_t g_fx                                  = 0;

        // A bone in the world, and that point on the screen. Nametags sit on
        // the head bone, so both have to exist before anything is drawn there.
        uintptr_t CPed__GetBonePosition    = 0;
        uintptr_t CSprite__CalcScreenCoors = 0;

        // --- World 2D, before the HUD ------------------------------------------
        //
        // plugin-sdk's drawHudEvent. The CALL to CHud::Draw inside Render2dStuff
        // - not the 0x53E4FF the event table names, which is the next
        // instruction and would run after the HUD had already painted.
        uintptr_t CHud__DrawCall = 0;

        // Re-applies RenderWare's 2D states. ImGui talks to D3D itself, so the
        // game's cached states are lies after a nametag flush; HUD and weapon
        // sprites then draw as white boxes. Called before CHud::Draw returns
        // to the rest of Render2dStuff.
        uintptr_t DefinedState2d = 0;

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

        // CCarCtrl::CreateCarForScript. What GTA's own mission scripts call:
        // pool allocation, world add, physics. Mission cleanup is the third
        // argument - we pass false so the game never deletes a Vanta car.
        uintptr_t CCarCtrl__CreateCarForScript = 0;

        // Paintjob is SetRemap. Upgrades are model ids 1000–1193; the
        // game attaches the mesh. Remove takes the same id Add was given.
        uintptr_t CVehicle__SetRemap             = 0;
        uintptr_t CVehicle__AddVehicleUpgrade    = 0;
        uintptr_t CVehicle__RemoveVehicleUpgrade = 0;

        // CVehicle::SetupRender paints the shared model materials from
        // ms_vehicleColourTable. Custom RGB is written into the first
        // four table slots for the duration of that call, then restored.
        uintptr_t CVehicle__SetupRender                      = 0;
        uintptr_t CVehicleModelInfo__ms_vehicleColourTable   = 0;

        // Written immediately before CreateCarForScript. GTA copies the
        // pair into CVehicle::m_anExtras and shows/hides extra_* frames.
        // MTA: VAR_CVehicle_Variation1/2.
        uintptr_t CVehicleModelInfo__ms_compsToUse = 0;

        // CModelInfo::ms_modelInfoPtrs — indexed by CEntity::m_nModelIndex.
        uintptr_t CModelInfo__ms_modelInfoPtrs = 0;

        // Per-instance custom plate texture. Create reads model->m_szPlateText.
        uintptr_t CVehicle__CustomCarPlate_TextureCreate  = 0;
        uintptr_t CVehicle__CustomCarPlate_TextureDestroy = 0;

        // Headlight beam / corona / reflection. Mid-function sites tint
        // the colours MTA patches; DoVehicleLights captures `this`.
        uintptr_t CVehicle__DoVehicleLights              = 0;
        uintptr_t CVehicle__DoHeadLightBeam              = 0;
        uintptr_t CVehicle__DoHeadLightBeam_Verts        = 0;
        uintptr_t CVehicle__DoHeadLightEffect_Corona1    = 0;
        uintptr_t CVehicle__DoHeadLightEffect_Corona2    = 0;
        uintptr_t CVehicle__DoHeadLightReflectionSingle_Shadow = 0;
        uintptr_t CVehicle__DoHeadLightReflectionTwin_Shadow = 0;
        uintptr_t CCoronas__RegisterCorona               = 0;
        uintptr_t CShadows__StoreCarLightShadow          = 0;

        // CPools::ms_pVehiclePool. CreateCarForScript allocates from it;
        // operator delete has no unique body, so we free through the pool
        // the same way the game does.
        uintptr_t CPools__ms_pVehiclePool = 0;

        // Occupancy tasks. Warp uses the simple SetPedIn/Out; walk-in uses
        // the complex Enter/Leave. ProcessPed is reached through the vtable.
        uintptr_t CTaskComplexEnterCarAsDriver__Constructor    = 0;
        uintptr_t CTaskComplexEnterCarAsPassenger__Constructor = 0;
        uintptr_t CTaskSimpleCarSetPedInAsDriver__Constructor  = 0;
        uintptr_t CTaskSimpleCarSetPedInAsPassenger__Constructor = 0;
        uintptr_t CTaskComplexLeaveCar__Constructor            = 0;
        uintptr_t CTaskSimpleCarSetPedOut__Constructor         = 0;
        uintptr_t CCarEnterExit__ComputeTargetDoorToEnterAsPassenger = 0;
        uintptr_t CCarEnterExit__ComputeTargetDoorToExit             = 0;
        uintptr_t CCarEnterExit__ComputePassengerIndexFromCarDoor    = 0;

        // Door position for passenger reach. Asking this on a vehicle with
        // no RenderWare object is a null deref inside GTA.
        uintptr_t CCarEnterExit__GetNearestCarPassengerDoor          = 0;

        // CVehicle::KillPedsInVehicle / KillPedsGettingInVehicle. A petrol
        // tank shot ends here: CEventVehicleDied and CEventDamage assume a
        // single-player player or an NPC, and a Vanta CPlayerPed in a seat
        // is the crash.
        uintptr_t CVehicle__KillPedsInVehicle        = 0;
        uintptr_t CVehicle__KillPedsGettingInVehicle = 0;

        // Per-component visuals. MTA calls these from SetDoorStatus /
        // SetPanelStatus; a full SetupDamageAfterLoad respawns every
        // missing part as a flying component.
        uintptr_t CAutomobile__FixDoor         = 0;
        uintptr_t CAutomobile__FixPanel        = 0;
        uintptr_t CAutomobile__SetBumperDamage = 0;
        uintptr_t CAutomobile__SetPanelDamage  = 0;
        uintptr_t CAutomobile__SetDoorDamage   = 0;

        uintptr_t gFireManager                       = 0;
        uintptr_t CFireManager__StartFire            = 0;
        uintptr_t CFireManager__ExtinguishPoint      = 0;
        uintptr_t CVehicle__ExtinguishCarFire        = 0;
        uintptr_t CFire__Extinguish                  = 0;
        uintptr_t CExplosion__ClearAllExplosions     = 0;
        uintptr_t CProjectileInfo__RemoveAllProjectiles = 0;

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
