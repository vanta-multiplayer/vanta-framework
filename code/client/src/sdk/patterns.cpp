#include "patterns.h"

#include <logging/logger.h>
#include <utils/hooking/hooking.h>
#include <utils/hooking/hooking_patterns.h>

#include <string_view>

namespace VantaMP::SDK {
    Patterns gPatterns;

    namespace {
        std::size_t gPatternScanCount = 0;

        // Reads the absolute address out of an instruction's operand.
        //
        // A global has no bytes of its own worth matching - .data holds a value,
        // not code, and every build lays it out differently. What is stable is
        // the instruction that touches it: `imul eax, eax, 0x190` followed by
        // `mov eax, [eax + <global>]` says "index the player array" in any build
        // that has a player array, and the address falls out of the operand.
        //
        // `offset` is where the operand sits inside the match, which differs per
        // instruction encoding and is recorded beside each call below.
        uintptr_t OperandAt(const void *site, std::size_t offset) {
            return *reinterpret_cast<const std::uint32_t *>(static_cast<const std::uint8_t *>(site) + offset);
        }

        // get_pattern hands back a pointer; the table stores addresses.
        uintptr_t Resolve(std::string_view signature) {
            ++gPatternScanCount;
            return reinterpret_cast<uintptr_t>(hook::get_pattern(signature));
        }

        // Resolves a signature and reads an absolute address from its operand.
        uintptr_t ResolveOperand(std::string_view signature, std::size_t offset) {
            ++gPatternScanCount;
            return OperandAt(hook::get_pattern(signature), offset);
        }

        // Resolves the destination of a relative opcode (e.g. CALL rel32).
        //
        // hook::get_opcode_address only takes a string literal, so the rel32
        // arithmetic is repeated here against the string_view get_pattern
        // overload: destination = site + 5 + rel32 stored at site + 1.
        uintptr_t ResolveOpcode(std::string_view signature) {
            ++gPatternScanCount;
            const auto *site = static_cast<const std::uint8_t *>(hook::get_pattern(signature));
            return reinterpret_cast<uintptr_t>(site + *reinterpret_cast<const std::int32_t *>(site + 1) + 5);
        }
    } // namespace

    void Patterns::InitPatterns() {
        gPatternScanCount = 0;
        // Signatures were derived from GTA:SA 1.0 US and proven to resolve to the
        // addresses named in the comments - see scripts/derive_pattern.py and
        // scripts/derive_global.py. The addresses are provenance, not values the
        // client uses: what ships is the signature.

        // --- World -----------------------------------------------------------

        // imul eax, eax, sizeof(CPlayerInfo) ; mov eax, [eax + CWorld::Players]
        //
        // The leading C7 and the trailing compare are both load-bearing: without
        // them the imul/mov pair alone matches 22 sites, because indexing that
        // array is a common thing for the game to do.
        gPatterns.CWorld__Players = ResolveOperand("C7 69 C0 90 01 00 00 8B 80 ? ? ? ? 3B C3 74", 9); // site 0x00570D05 -> 0x00B7CD98

        gPatterns.CTimer__ms_fTimeScale                 = ResolveOperand("C7 00 00 C7 05 ? ? ? ? 00 00 80 3F C3", 5); // site 0x00721D3D -> 0x00B7CB64
        gPatterns.CClock__ms_nMillisecondsPerGameMinute = ResolveOperand("8B 35 ? ? ? ? 32", 2); // site 0x0052CF22 -> 0x00B7015C
        gPatterns.CClock__ms_nGameClockHours            = ResolveOperand("80 3D ? ? ? ? 05 72", 2); // site 0x0045577D -> 0x00B70153
        gPatterns.CClock__ms_nGameClockMinutes          = ResolveOperand("80 3D ? ? ? ? 3C", 2); // site 0x0052CF71 -> 0x00B70152
        gPatterns.CWeather__ForceWeatherNow             = Resolve("E9 ? ? ? ? 66 A3"); // 0x0072A4F0
        gPatterns.CGame__ms_fGravity                    = ResolveOperand("20 D8 0D ? ? ? ? D9 44 24 3C D8 0D", 3); // site 0x0067EC84 -> 0x00863984
        gPatterns.CRenderer__ms_fFarClipPlane           = ResolveOperand("8B 15 ? ? ? ? A1 3C 70 C1 00 52 50 E8 15", 2); // site 0x0053DC79 -> 0x00B7C4F0
        gPatterns.CRenderer__ms_fFogStart               = ResolveOperand("06 2B 00 D9 05", 5); // site 0x0053DC88 -> 0x00B7C4F4
        gPatterns.CWeather__Rain                        = ResolveOperand("E8 13 C8 00 D8 0D F0 12 C8 00 D8 0D", 12); // site 0x0072B345 -> 0x00C81324
        gPatterns.CWater__ms_fWaveHeight                = ResolveOperand("C7 05 ? ? ? ? 00 00 80 3F EB 06 D9 1D E8", 2); // site 0x0072C659 -> 0x00C812E8
        gPatterns.CTimeCycle__m_nSkyTopRed              = ResolveOperand("00 90 90 90 90 90 66 A1", 8); // site 0x0053D49A -> 0x00B7C4C4
        gPatterns.CTimeCycle__m_nSunCoreRed             = ResolveOperand("D2 C4 B7 00 D9 1C 24 51 33 C9 8A 0D", 12); // site 0x006FC695 -> 0x00B7C4D0
        gPatterns.CTimeCycle__m_fSunSize                = ResolveOperand("C3 90 90 90 90 90 90 90 90 90 D9 05 ? ? ? ? D8 0D 1C", 12); // site 0x006FA9E6 -> 0x00B7C4DC
        gPatterns.CCoronas__MoonSize                    = ResolveOperand("8B 0D ? ? ? ? 83 C4 18 85", 2); // site 0x00713B10 -> 0x008D4B60
        gPatterns.CTimeCycle__m_fWaterRed               = ResolveOperand("D9 05 ? ? ? ? D8 C9 E8 D4", 2); // site 0x0051295F -> 0x00B7C508
        gPatterns.CWater__UnderwaterRGBA                = ResolveOperand("8B 1D ? ? ? ? D8 05", 2); // site 0x00705187 -> 0x008D5140
        gPatterns.CPostEffects__HeatHazeHourStart       = ResolveOperand("8B 3D ? ? ? ? 0F B6 F3", 2); // site 0x0072C176 -> 0x008D50D4
        gPatterns.CPostEffects__HeatHazeRandomShift     = ResolveOperand("89 35 ? ? ? ? C7 05 EC 50 8D 00 0C", 2); // site 0x0070148B -> 0x00C402C0
        gPatterns.CPostEffects__HeatHazeScanSizeX       = ResolveOperand("89 3D ? ? ? ? D8 0D 24", 2); // site 0x00701549 -> 0x00C40304
        gPatterns.CPostEffects__bHeatHazeFX             = ResolveOperand("C3 90 90 90 8A 44 24 04 A2 ? ? ? ? C3 90 90 90 90 90 90 8A", 9); // site 0x0070115C -> 0x00C402BA
        gPatterns.CPostEffects__HeatHazeAutoType        = Resolve("83 EC 08 53 8B 1D"); // 0x00701455
        gPatterns.CClouds__VolumetricRender             = Resolve("A1 14 29 B7 00 83 EC 7C"); // 0x00716380
        gPatterns.CClouds__BottomRender                 = Resolve("83 EC 44 0F B7 05"); // 0x007154B0
        gPatterns.CClouds__SkylineRender                = Resolve("D9 41 08 D9 C9 D9 F3 EB"); // 0x0071411A
        gPatterns.CPlane__TrailRender                   = Resolve("83 EC 08 D9 44 24 0C 53"); // 0x00717180
        gPatterns.CWorld__fJetpackMaxHeight             = ResolveOperand("D8 05 ? ? ? ? D8 5D 08", 2); // site 0x0067F25A -> 0x008703D8
        gPatterns.CWeather__WindVel                     = ResolveOperand("00 D9 05 ? ? ? ? D8 0D F0 12 C8 00 D8 0D 1C", 3); // site 0x0072B2AF -> 0x00C813E0

        // --- Boot: loading screens and the frontend ---------------------------

        gPatterns.CGame__Init3Call                = Resolve("E8 ? ? ? ? 83 C4 04 B0 01 5E C3 90"); // 0x0053BCD9
        gPatterns.CLoadingScreen__Draw            = Resolve("8B 44 24 04 85 C0 74 0E 8B 4C"); // 0x0053DED0
        gPatterns.CLoadingScreen__NewChunk        = Resolve("51 A0 18 B3 BA 00 84 C0 74"); // 0x00590D00
        gPatterns.CLoadingScreen__DrawCall1       = Resolve("E8 ? ? ? ? 83 C4 04 59 C3 A1"); // 0x00590D7C
        gPatterns.CLoadingScreen__DrawCall2       = Resolve("E8 ? ? ? ? A1 44 09"); // 0x00590DB3
        gPatterns.CLoadingScreen__DrawReturn      = Resolve("E9 ? ? ? ? D8 1D 80"); // 0x00590D9F
        gPatterns.CLoadingScreen__AudioCall       = Resolve("E8 ? ? ? ? E8 80 58"); // 0x00748CF6
        gPatterns.CGame__StateResetToZero         = Resolve("89 35 C0 D4 C8 00"); // 0x00747483
        gPatterns.CMenuManager__ProcessCall       = Resolve("E8 ? ? ? ? B9 90 BC B6 00 C6"); // 0x0057C2BC
        gPatterns.CMenuManager__DrawStandardMenus = Resolve("57 E8 ? ? ? ? EB 0C"); // 0x0057BA57
        gPatterns.CMenuManager__InputCall         = Resolve("E8 ? ? ? ? 8B CE E8 FD D8"); // 0x0057B457
        gPatterns.CMenuManager__BackgroundRect    = Resolve("E8 ? ? ? ? A0 98"); // 0x0057B9BB
        gPatterns.CMenuManager__BackgroundTexture = Resolve("74 36 68 FF 00 00"); // 0x0057B9CA
        gPatterns.CMenuManager__SetRenderStates   = Resolve("A1 24 7B C9 00 6A 00 6A 06 FF 50 20 8B 0D 24 7B C9 00 6A 00 6A 08 FF 51 20 8B 15 24"); // 0x00573A60
        gPatterns.CMenuManager__EscapeHandler1    = Resolve("75 38 0F B6 86 EA"); // 0x00576B8D
        gPatterns.CMenuManager__EscapeHandler2    = Resolve("75 16 80 3D 88 30 A4 00 01 74 0D 38"); // 0x00576BAF
        gPatterns.CMenuManager__PauseCheckProbe   = Resolve("FF 53 E8 2C 74 E4"); // 0x00748ADD
        gPatterns.CMenuManager__PauseCheckAlt     = Resolve("0F 84 20 03 00 00"); // 0x00748A8D
        gPatterns.CMenuManager__OnFocusLoss       = Resolve("A0 31 68 BA 00 84 C0 75 10"); // 0x0053BC60
        gPatterns.CMenuManager__AfterAltTabFlag   = Resolve("01 C3 90 90 90 90 90 90 56"); // 0x0053BC78
        gPatterns.Print__AspectRatio              = Resolve("E8 ? ? ? ? D9 44 24 14 D8 1D 34"); // 0x00745997
        gPatterns.Print__SoundManager             = Resolve("E8 ? ? ? ? 8B 4C 24 14 83 C4 04 5F"); // 0x005B97C9

        // Globals, read out of the instruction that references them.
        gPatterns.CTimer__UserPause   = ResolveOperand("51 A0 ? ? ? ? 84 C0 56 57 8B", 2); // site 0x004D6DF0 -> 0x00B7CB49
        gPatterns.CMenuManager__Pause = ResolveOperand("1F 00 A0 ? ? ? ? 84 C0 74", 3); // site 0x0053E81D -> 0x00BA67A4
        gPatterns.gGameState          = ResolveOperand("83 3D ? ? ? ? 08 75 63", 2); // site 0x00730EDD -> 0x00C8D4C0
        gPatterns.RsGlobal            = ResolveOperand("C7 05 ? ? ? ? 88 D3", 2); // site 0x00619602 -> 0x00C17040

        // --- Script and the local player --------------------------------------

        gPatterns.CRunningScript__ProcessOneCommand = Resolve("66 FF 05 F4 47 A4 00 8B 51"); // 0x00469EB0
        gPatterns.CRunningScript__OpcodeTable       = Resolve("60 5E 46 00 E0 6D"); // 0x008A6168
        gPatterns.CRunningScript__ProcessCall       = Resolve("E8 ? ? ? ? 3B FB 8B"); // 0x0046A21B
        gPatterns.ScmLoadJump                       = Resolve("68 6C 9D 85 00 E8"); // 0x00468EB5
        gPatterns.ScmLoadSkipTarget                 = Resolve("68 54 8B 85 00 E8 DD"); // 0x00468EE9
        gPatterns.CPlayerPed__SetupPlayerPed        = Resolve("6A FF 68 7B ED 83"); // 0x0060D790
        gPatterns.CPlayerPed__ModelIdPush           = Resolve("6A 00 8B CE E8 78"); // 0x0060D5FF
        gPatterns.FindPlayerPed                     = Resolve("8B 44 24 04 85 C0 7D 07 0F B6 05 74 CD B7 00 69 C0 90 01 00 00 8B 80 98 CD B7 00 C3"); // 0x0056E210
        gPatterns.CStreaming__LoadScene             = Resolve("83 EC 1C 55 8B 6C"); // 0x0040EB70
        gPatterns.CPlaceable__SetPosn               = Resolve("E9 ? ? ? ? 7A 4E"); // 0x00420B80

        // CEntity::UpdateRwMatrix / CEntity::UpdateRwFrame. Writing
        // CMatrix::right/up/at changes the physics matrix only; the renderer
        // draws from the attached RwMatrix and the frame hierarchy, and both
        // have to be pushed after a pose write. MTA calls exactly these two,
        // in this order, at the end of CEntitySA::SetMatrix.
        gPatterns.CEntity__UpdateRwMatrix           = Resolve("E9 ? ? ? ? 55 72"); // 0x00446F90
        gPatterns.CEntity__UpdateRwFrame            = Resolve("8B 41 18 85 C0 74 0A"); // 0x00532B00
        gPatterns.CCamera__Fade                     = Resolve("E9 ? ? ? ? 53 8A 59"); // 0x0050AC20
        gPatterns.CCamera__SetFadeColour            = Resolve("8A 44 24 04 3C 02"); // 0x0050BF00

        gPatterns.CWorld__PlayerInFocus = ResolveOperand("3B F8 75 19 0F B6 0D", 7); // site 0x004B5C58 -> 0x00B7CD74
        gPatterns.TheCamera             = ResolveOperand("50 56 B9 ? ? ? ? C6", 3); // site 0x00520D82 -> 0x00B6F028
        gPatterns.CameraSize            = Resolve("83 EC 34 57 8B 7C 24 3C 85 FF"); // 0x0072FC70
        gPatterns.RwCameraClear         = Resolve("8B 4C 24 0C A1 24 7B C9 00"); // 0x007EE340
        gPatterns.CGame__Process        = Resolve("83 EC 0C 53 56 57 E8"); // 0x0053BEE0

        gPatterns.RwD3D9ReleaseVideoMemory = Resolve("53 83 C9 FF 33 DB"); // 0x007F7F70
        gPatterns.RwD3D9RestoreRasterList  = Resolve("83 EC 60 E9 3D 90"); // 0x004CC970
        gPatterns.RwD3D9RestoreAfterReset1 = Resolve("8B 0D 1C BF C9 00 53"); // 0x007F58D0
        gPatterns.RwD3D9RestoreAfterReset2 = Resolve("83 EC 0C A1 F0 92"); // 0x007FD100
        gPatterns.RwD3D9RestoreAfterReset3 = Resolve("A1 1C BF C9 00 83 EC 20"); // 0x007FB480
        gPatterns.RwD3D9RestoreAfterReset4 = Resolve("A1 1C BF C9 00 83 EC 28"); // 0x0080E020
        gPatterns.CPostEffects__SetupBackBufferVertex = Resolve("55 8B EC 83 E4 F8 83 EC 20 D9"); // 0x007043D0
        gPatterns.CPostEffects__pRasterFrontBuffer    = ResolveOperand("8B F8 A1 ? ? ? ? 85 C0 89 7C 24 24 74", 3); // site 0x0070446D -> 0x00C402D8
        gPatterns.RwRasterDestroy                     = Resolve("56 8B 74 24 08 56 68 ? ? ? ? E8 ? ? ? ? A1 24 7B C9 00 6A 00 56 6A 00"); // 0x007FB020
        gPatterns.D3DPresentParameters     = ResolveOperand("BE ? ? ? ? 8D 7C 24 20 F3 A5", 1); // site 0x007F867D -> 0x00C9C040
        gPatterns.RwD3D9RenderSurface      = ResolveOperand("68 ? ? ? ? 57 50 8B 10 FF 92 98", 1); // site 0x007F86DC -> 0x00C97C30

        // mov eax, [GnumSubSystems]; mov edi, 1; cmp eax, edi; jle single
        gPatterns.PsSelectDeviceHook    = Resolve("A1 ? ? ? ? BF 01 00 00 00 3B C7 7E"); // 0x00746219
        gPatterns.VidModeDialogFuncPush = Resolve("68 ? ? ? ? 55 6A 68 53 FF 15"); // 0x00746238
        gPatterns.SavedVideoMode        = ResolveOperand("75 29 E8 ? ? ? ? 8B 0D ? ? ? ? 83 3C 88 00", 9); // site 0x0074629A -> 0x00BA6820
        gPatterns.GcurSelVM             = ResolveOperand("89 0D ? ? ? ? 8B 15 ? ? ? ? 52 8D 44 24 18", 2); // site 0x007462BF -> 0x008D6220
        gPatterns.RwEngineGetVideoModeInfo = Resolve("8B 44 24 08 8B 0D ? ? ? ? 56 8B 74 24 08 50 6A 00 56 83 C1 10 6A 06"); // 0x007F2CF0
        gPatterns.RwEngineGetNumVideoModes = Resolve("51 8B 0D ? ? ? ? 6A 00 8D 44 24 04 6A 00 50 83 C1 10 6A 05"); // 0x007F2CC0
        gPatterns.SetVideoMode             = Resolve("8B 44 24 04 50 E8 ? ? ? ? 83 C4 04 E8 ? ? ? ? A1"); // 0x00745C70

        // --- Input -----------------------------------------------------------

        gPatterns.DirectInput__MouseExclusiveMode = Resolve("8A 44 24 04 8B 0D"); // 0x007469A0
        gPatterns.DirectInput__MouseReinit1       = Resolve("74 0F E8 9D 02 1D"); // 0x00576CCC
        gPatterns.DirectInput__MouseReinit2       = Resolve("74 0E E8 AF 00 1D"); // 0x00576EBA
        gPatterns.DirectInput__MouseReinit3       = Resolve("74 0E E8 DF FF 1C"); // 0x00576F8A
        gPatterns.Cursor__Recentre                = Resolve("83 EC 10 56 8B 74 24 18 D9"); // 0x007453F0
        gPatterns.Cursor__Hiding                  = Resolve("6A 00 FF 15 EC 82 85 00 6A"); // 0x007481CD

        gPatterns.IsForegroundApp = ResolveOperand("A1 1C 62 8D 00 85 C0 0F", 1); // site 0x00746060 -> 0x008D621C

        // --- Gameplay the server owns instead ---------------------------------

        gPatterns.CStats__MessageDisplay1    = Resolve("51 56 6A 00 E8 E7"); // 0x0055B980
        gPatterns.CStats__MessageDisplay2    = Resolve("6A 00 E8 09 64 FE"); // 0x00559760
        gPatterns.CStats__Increment          = Resolve("51 D9 44 24 0C D8 1D"); // 0x0055C180
        gPatterns.CStats__Decrement1         = Resolve("D9 14 8D 80 93 B7 00 DF"); // 0x00559FD5
        gPatterns.CStats__Decrement2         = Resolve("D9 1C 8D 80 93 B7 00 EB"); // 0x00559FEB
        gPatterns.CHud__VitalStatsBranch     = Resolve("75 0E C6 05 DE B1"); // 0x0058FC3C
        gPatterns.CPed__HungerMessage        = Resolve("E8 ? ? ? ? 83 C4 10 C6 05 F0"); // 0x0056E740
        gPatterns.SurfaceInfos__CantSprintOn = Resolve("8B 44 24 04 8D 04 40 8B 84 81 94 00 00 00 C1 E8 1C"); // 0x0055E870
        gPatterns.Weapon__ThrowableAccuracy  = Resolve("0F 84 86 00 00 00 8B 47"); // 0x00742685
        gPatterns.CTask__CarJackSelect       = Resolve("74 3A E8 23 B0 FD"); // 0x0063F576
        gPatterns.CPostEffects__SpeedBlur    = Resolve("E8 ? ? ? ? 83 C4 04 8A 0D"); // 0x00704E8A
        gPatterns.CPedStats__HeadingRate     = Resolve("C7 44 28 20 00 00"); // 0x005BFA1D
        gPatterns.CTimer__FrameDelayJump     = Resolve("E8 ? ? ? ? E8 13 31"); // 0x0053E923
        gPatterns.CTimer__FrameDelayStackFix = Resolve("14 83 C4 04 85 C0 5E"); // 0x0053E99F
        gPatterns.CTimer__FrameDelayPopFix   = Resolve("5E 0F 84 5D 02 00"); // 0x0053E9A5
        gPatterns.Weapon__StrafeDelta        = Resolve("D9 05 5C CB B7 00 8B 54 24 04"); // 0x0061E0C0
        gPatterns.Weapon__StrafeMultiply     = Resolve("D8 0D A8 8C 85 00 D9 42"); // 0x0061E0CA
        gPatterns.CCamera__DeathFade         = Resolve("55 68 00 00 00 40 B9 28 F0 B6 00 E8 29"); // 0x00442DE7
        gPatterns.CGarages__Update           = Resolve("E8 ? ? ? ? E8 4F 4C"); // 0x0053C0B7
        gPatterns.CCamera__IdleProcess       = Resolve("51 56 8B F1 E8 77"); // 0x00522C80
        gPatterns.CMenuManager__SaveSettings = Resolve("83 EC 0C 56 57 8B F9 E8"); // 0x0057C660
        gPatterns.CMenuManager__LoadSettings = Resolve("83 EC 44 56 57 8B"); // 0x0057C8F0

        // Pause sits at CMenuManager+0x5C. The two volume bytes are earlier
        // in that same object; writing them without this base is a guess.
        gPatterns.CMenuManager = gPatterns.CMenuManager__Pause - 0x5C;

        // mov ecx, AudioEngine immediately after the Process call.
        gPatterns.CAudioEngine = OperandAt(reinterpret_cast<const void *>(gPatterns.CMenuManager__ProcessCall), 6); // 0x00B6BC90

        // The two functions share a prologue. The last byte of the relative
        // call is what separates music from effects.
        gPatterns.CAudioEngine__SetMusicMasterVolume   = Resolve("0F BE 44 24 04 89 44 24 04 51 B9 B8 F8 B5 00 DB 44 24 08 D8 0D D4 A7 85 00 D9 1C 24 E8 2F"); // 0x00506DE0
        gPatterns.CAudioEngine__SetEffectsMasterVolume = Resolve("0F BE 44 24 04 89 44 24 04 51 B9 B8 F8 B5 00 DB 44 24 08 D8 0D D4 A7 85 00 D9 1C 24 E8 0F"); // 0x00506E10

        gPatterns.CGameLogic__Update         = Resolve("83 EC 4C 53 55 56 57 33"); // 0x00442AD0
        gPatterns.CTrain__ForbiddenCross     = Resolve("83 EC 18 53 56 68"); // 0x004418E0
        gPatterns.CSamSite__Behaviour        = Resolve("83 EC 34 53 56 57 8B F9 8B 87 40"); // 0x005A07D0
        gPatterns.CReplay__FinishPlayback    = Resolve("A0 88 30 A4 00 83 EC 0C 3C"); // 0x0045F050
        gPatterns.CReplay__TriggerPlayback   = Resolve("E9 ? ? ? ? 62 64"); // 0x004600F0
        gPatterns.CReplay__Update            = Resolve("A0 52 F8 B5 00 84 C0 0F 85 18"); // 0x00460500
        gPatterns.CReplay__PlayFromHD        = Resolve("55 E8 ? ? ? ? 68 6C 9C 85 00 68 60"); // 0x00460390

        // --- Widescreen --------------------------------------------------------

        gPatterns.Widescreen__Check1      = Resolve("0F 8C B1 00 00 00 D9"); // 0x00745B81
        gPatterns.Widescreen__Check2      = Resolve("0F 8C E2 00 00 00 81"); // 0x0074596C
        gPatterns.Widescreen__Nop1        = Resolve("00 00 81 7C 24 14 E0"); // 0x00745970
        gPatterns.Widescreen__Nop2        = Resolve("00 00 D9 44 24 08 D8 1D 98"); // 0x00745B85
        gPatterns.Widescreen__Nop3        = Resolve("7A 71 8B 54 24 18"); // 0x007459E1
        gPatterns.CDraw__CalculateAspect  = Resolve("A0 93 67 BA 00 84 C0 A0"); // 0x006FF420
        gPatterns.CHud__AspectMultiplier  = Resolve("25 49 12 3B 50 41"); // 0x00859524
        gPatterns.CCamera__CrosshairScale = Resolve("00 00 40 43 00 00"); // 0x00866C74

        gPatterns.CDraw__AspectRatio = ResolveOperand("8B 0D ? ? ? ? D8 0D D4", 2); // site 0x0053EB26 -> 0x00C3EFA4

        // --- Player slots ------------------------------------------------------
        //
        // Each of these signatures carries the array it allocates for, as the
        // size and address the game pushes right after the count - so they
        // document what they are without a separate comment.
        gPatterns.Alloc__PlayerInfo1 = Resolve("6A 02 68 90 01 00 00 68 98 CD B7 00 E8 95"); // 0x0084E98A
        gPatterns.Alloc__PlayerInfo2 = Resolve("6A 02 68 90 01 00 00 68 98 CD B7 00 E8 EC"); // 0x00856505
        gPatterns.Alloc__Pad1        = Resolve("6A 02 68 34 01 00 00 68 58 34 B7 00 E8 25"); // 0x0084E1FA
        gPatterns.Alloc__Pad2        = Resolve("6A 02 68 34 01 00 00 68 58 34 B7 00 E8 8C"); // 0x00856465

        gPatterns.CPad__Pads = ResolveOperand("00 B9 ? ? ? ? E9 C0", 2); // site 0x0040380C -> 0x00B73458

        // --- Remote players ----------------------------------------------------

        gPatterns.CPlayerPed__ProcessControl       = Resolve("83 EC 34 53 55 56 8B F1 8B"); // 0x0060EA90
        gPatterns.CPlayerPed__ProcessControlVtable = Resolve("90 EA 60 00 B0 DF"); // 0x0086D190
        gPatterns.CPlayerPed__GetPlayerInfo        = Resolve("33 C0 BA 98 CD B7"); // 0x00609FF0
        gPatterns.CPad__GetPad                     = Resolve("8B 44 24 04 69 C0 34"); // 0x0053FB70
        gPatterns.CPad__Clear                      = Resolve("56 8B F1 E8 08 D5"); // 0x00541A70
        gPatterns.CPad__ClearMouseHistory          = Resolve("32 C0 A2 04 34 B7"); // 0x00541BD0

        // CGame::Process: sub esp,0xC / push ebx,esi,edi / call UpdatePads
        gPatterns.CPad__UpdatePadsCall = Resolve("83 EC 0C 53 56 57 E8") + 6; // call at 0x0053BEE6 -> 0x00541DD0

        gPatterns.CPad__NewMouseState = ResolveOperand("FF A0 ? ? ? ? 83 C4 0C 3A", 2); // site 0x0057E51E -> 0x00B73418

        // Tasks allocate from their own pool, not the ped one - the signature
        // differs from CPed::operator new only in the pool it loads.
        gPatterns.CTask__operator_new            = Resolve("8B 0D A8 44 B7 00 E9"); // 0x0061A5A0
        gPatterns.CTaskSimpleDuck__Constructor   = Resolve("56 8B F1 E8 C8 83"); // 0x00691FC0
        // Hoodlum stub, same shape as CTaskSimpleCarSetPedInAsPassenger.
        // MTA: FUNC_CTaskSimpleJetPack__Constructor 0x67B4E0.
        gPatterns.CTaskSimpleJetPack__Constructor = Resolve("6A FF E9 3D C9 D8"); // 0x0067B4E0
        // MTA: CPedIntelligence::GetTaskJetPack 0x601110.
        gPatterns.CPedIntelligence__GetTaskJetPack = Resolve("56 8B F1 8B 0E E8 D6"); // 0x00601110
        gPatterns.CTaskManager__GetSimplestActiveTask = Resolve("33 C0 83 3C 81 00 75 20"); // 0x006819D0
        gPatterns.CTaskManager__SetTaskSecondary = Resolve("53 8B 5C 24 08 56 57 8B 7C 24 14 8B F1 8B"); // 0x00681B60
        gPatterns.CTaskManager__GetTaskSecondary = Resolve("8B 44 24 04 8B 44 81 14"); // 0x00681810

        gPatterns.CTaskSimpleUseGun__Constructor  = Resolve("6A FF 68 D8 F3 83"); // 0x0061DE60
        gPatterns.CTaskSimpleUseGun__ControlGun   = Resolve("55 8B 6C 24 0C 56 8B F1 8B 4E"); // 0x0061E040
        gPatterns.CPedIntelligence__GetTaskUseGun = Resolve("56 8D 71 04 6A 00 8B CE E8 93"); // 0x00600F70

        // Near-identical prologues, so both carry the call that follows: the
        // duck constructor above starts with the same four bytes and is told
        // apart only by its relative offset.
        gPatterns.CTaskComplexInWater__Constructor = Resolve("56 8B F1 E8 D8 52"); // 0x006350D0
        gPatterns.CTaskManager__SetTask            = Resolve("53 8B 5C 24 08 85 DB 56 57 8B 7C"); // 0x00681AF0

        gPatterns.CCamera__Rotation  = ResolveOperand("FF D8 25 ? ? ? ? D9 5C 24 20", 3); // site 0x00688FA6 -> 0x00B6F178
        gPatterns.CCamera__MouseLook = ResolveOperand("50 A0 ? ? ? ? 64 89 25 00 00 00 00 83 EC 74", 2); // site 0x0060B65D -> 0x00B6EC2E

        // --- Game methods called directly --------------------------------------

        gPatterns.CPed__operator_new                 = Resolve("8B 0D 90 44 B7 00 E9"); // 0x005E4720

        // Its matching delete has no unique signature of its own - the body is
        // too short and too ordinary - so it is reached through a call to it,
        // which get_opcode_address follows to the target.
        gPatterns.CPed__operator_delete = ResolveOpcode("E8 ? ? ? ? 83 C4 04 8B C6 5E C2 04 00 90 90 83 EC 20 53 55 56 57"); // call at 0x0060A9B0 -> 0x005E4760
        gPatterns.CPlayerPed__Constructor            = Resolve("6A FF 68 58 ED 83"); // 0x0060D5B0
        gPatterns.CPed__SetModelIndex                = Resolve("56 8B F1 81 4E 1C"); // 0x005E4880
        gPatterns.CPlaceable__SetOrientation         = Resolve("83 EC 0C 56 8B F1 8B 4E 14"); // 0x00439A80
        gPatterns.CPedIntelligence__FlushImmediately = Resolve("6A FF 68 26 E8 83"); // 0x00601640
        gPatterns.CPlayerPed__SetInitialState        = Resolve("64 A1 00 00 00 00 6A FF 68 41 ED"); // 0x0060CD20
        gPatterns.CPedClothesDesc__Constructor       = Resolve("8B D1 57 33 C0 B9 0A 00 00 00 8B FA F3 AB 8D 7A 28 B9 12 00 00 00 F3 AB 89 42 70 89 42 74 8B"); // 0x005A8020
        gPatterns.CPedClothesDesc__Initialise        = Resolve("8B D1 57 33 C0 B9 0A 00 00 00 8B FA F3 AB 8D 7A 28 B9 12 00 00 00 F3 AB 89 42 70 89 42 74 5F"); // 0x005A78F0
        gPatterns.CWorld__Add                        = Resolve("56 8B 74 24 08 8B 46 18 85 C0 74"); // 0x00563220
        gPatterns.CWorld__Remove                     = Resolve("56 8B 74 24 08 8B 06 8B CE FF 50 0C"); // 0x00563280

        gPatterns.CPed__GiveWeapon       = Resolve("51 53 8B 5C 24 0C 55 56 57"); // 0x005E6080
        gPatterns.CPed__SetCurrentWeapon = Resolve("53 8B 5C 24 08 83 FB FF 56 8B"); // 0x005E61F0
        gPatterns.CPed__ClearWeapon      = Resolve("56 57 8B 7C 24 0C 6A 01 57 8B F1 E8 A0"); // 0x005E62B0
        gPatterns.CPed__SetMoveAnim      = Resolve("56 8B F1 8B 86 30 05 00 00 83 F8 36 0F"); // 0x005E4A00
        // mov eax, alpha; mov ecx, clump; mov edx, [ms_clumpPluginOffset]; mov [edx+ecx+4], eax
        gPatterns.CVisibilityPlugins__SetClumpAlpha = Resolve("8B 44 24 08 8B 4C 24 04 8B 15 ? ? ? ? 89 44 0A 04"); // 0x00732B00

        // --- Streaming ----------------------------------------------------------

        gPatterns.CStreaming__RequestModel           = Resolve("53 8B 5C 24 0C 55 8B 6C 24 0C 56 57 8D"); // 0x004087E0
        gPatterns.CStreaming__LoadAllRequestedModels = Resolve("E9 ? ? ? ? 56 33 F6 84 C0 0F"); // 0x0040EA10
        gPatterns.CStreaming__HasModelLoaded         = Resolve("8B 44 24 04 8D 04 80 8A 14 85 D0"); // 0x004044C0

        // mov ecx, [CGame::currArea]; test ecx, ecx
        gPatterns.CGame__CurrArea = ResolveOperand("8B 0D ? ? ? ? 85 C9 8B 54", 2); // site 0x0040471F -> 0x00B72914
        gPatterns.CStreaming__RemoveBuildingsNotInArea = Resolve("53 55 8B 6C 24 0C 56 57 8B 3D"); // 0x004094B0

        gPatterns.CWeaponInfo__GetWeaponInfo = Resolve("8A 4C 24 08 84 C9 E9"); // 0x00743C60

        // --- Where a ped points its gun -----------------------------------------

        gPatterns.CPedIK__PointGunInDirection = Resolve("83 EC 10 53 55 8B E9 8B"); // 0x005FDC00

        // The three places the game calls it from. Each signature is the call
        // itself plus the code that follows, the call bytes alone being the same
        // five bytes everywhere.
        gPatterns.CPedIK__PointGunInDirectionCallA = Resolve("E8 ? ? ? ? 5F 5E 83 C4 1C C2 08"); // 0x005FDF7A
        gPatterns.CPedIK__PointGunInDirectionCallB = Resolve("E8 ? ? ? ? 8B 4F 30 8B 51 18 C1 EA 0B F6 C2 01 74 2F"); // 0x0061F351
        gPatterns.CPedIK__PointGunInDirectionCallC = Resolve("E8 ? ? ? ? 5F 5E 5D 5B 83 C4 34 C2 04"); // 0x0062876D

        // sub esp, 3Ch; push ebx; push esi; push edi; mov esi, ecx; mov eax, [esi]
        gPatterns.CWeapon__Fire = Resolve("83 EC 3C 53 56 57 8B F1 8B 06 6A 01"); // 0x00742300

        gPatterns.CWeapon__FireInstantHit = Resolve("6A FF 68 BB 8E 84"); // 0x0073FB10

        // The CALL to ProcessLineOfSight inside FireInstantHit, and the CALL to
        // CPed::IsPlayer that would send a remote CPlayerPed down the camera path.
        gPatterns.CWeapon__FireInstantHit_LosCall      = Resolve("E8 ? ? ? ? 83 C4 40 8B 4C"); // 0x00740B69
        gPatterns.CWeapon__FireInstantHit_IsPlayerCall = Resolve("E8 ? ? ? ? 84 C0 0F 84 68 04"); // 0x0074034C

        gPatterns.CWorld__ProcessLineOfSight = Resolve("83 EC 60 66 81 3D"); // 0x0056BA00
        gPatterns.CPed__IsPlayer             = Resolve("8B 81 98 05 00 00 85 C0 74 08"); // 0x005DF8F0

        gPatterns.CCamera__Find3rdPersonCamTargetVector = Resolve("83 EC 10 0F B6 41"); // 0x00514970

        gPatterns.CWorld__pIgnoreEntity = ResolveOperand("39 35 ? ? ? ? 0F 84 C8", 2); // site 0x004153B4 -> 0x00B7CD68

        gPatterns.CPed__DoGunFlash                       = Resolve("56 8B F1 8B 86 F4 04"); // 0x005DF340
        gPatterns.CPed__GetTransformedBonePosition       = Resolve("8A 44 24 0C 83 EC 0C 56 8B F1 84 C0 8B 86 74 04 00 00 57 74 50"); // 0x005E01C0
        gPatterns.Fx_c__TriggerGunshot                   = Resolve("A1 3C F0 B6 00 83 EC 24 85 C0 56"); // 0x004A0DE0
        gPatterns.CWeapon__AddGunshell                   = Resolve("83 EC 28 56 8B 74 24 30"); // 0x0073A3E0
        gPatterns.CAEPedWeaponAudioEntity__AddAudioEvent = Resolve("8A 81 A0 00 00 00"); // 0x004E69F0
        gPatterns.CPointLights__AddLight                 = Resolve("8B 0D 3C F0 B6 00 83 EC 10"); // 0x007000E0
        gPatterns.g_fx                                   = ResolveOperand("52 B9 ? ? ? ? E8 D4 92", 2); // site 0x00475D61 -> 0x00A9AE00

        gPatterns.CPed__GetBonePosition    = Resolve("8A 44 24 0C 83 EC 0C 56 8B F1 84 C0 8B 86 74 04 00 00 57 74 51"); // 0x005E4280
        gPatterns.CSprite__CalcScreenCoors = Resolve("83 EC 14 A1 A0 EF"); // 0x0070CE30

        // The CALL to CHud::Draw, which is plugin-sdk's drawHudEvent in
        // Render2dStuff. Hooked before the HUD so a nametag is world 2D.
        gPatterns.CHud__DrawCall = Resolve("E8 ? ? ? ? E8 DC 15"); // 0x0053E4FA

        // Differs from DefinedState only in the last push (0 vs 1).
        gPatterns.DefinedState2d = Resolve("A1 24 7B C9 00 6A 01 6A 02 FF 50 20 8B 0D 24 7B C9 00 6A 00"); // 0x00734750

        // --- Running more than one client --------------------------------------

        gPatterns.MultiInstanceCheckCall = Resolve("E8 ? ? ? ? 85 C0 75 0F 53"); // 0x0074872D
        gPatterns.IsAlreadyRunning       = Resolve("A1 24 62 8D 00 50"); // 0x007468E0
        gPatterns.CdStreamSemaphoreName  = ResolveOperand("00 68 ? ? ? ? A3 EC", 2); // site 0x00406944 -> 0x00858AD4

        // --- Population ------------------------------------------------------

        gPatterns.CPopulation__Add               = Resolve("6A FF 68 8E EF 83"); // 0x00614720
        gPatterns.CPopulation__ManagePed         = Resolve("51 56 8B 74 24 0C 8B CE E8 23"); // 0x00611FC0
        gPatterns.CPopulation__RemovePed         = Resolve("56 8B 74 24 08 56 E8 55 23"); // 0x00610F20
        gPatterns.CPopulation__UpdateEarlyOut    = Resolve("E8 ? ? ? ? A0 23"); // 0x00616698
        gPatterns.CCarCtrl__GenerateRandomCars   = Resolve("A0 51 F8 B5 00 84 C0 74 08"); // 0x004341C0
        gPatterns.CCarCtrl__RemoveCarsIfPoolFull = Resolve("A1 4C CB B7 00 83 E0 07 83 EC 08 3C 03"); // 0x004322B0
        gPatterns.CCarCtrl__CreateCarForScript   = Resolve("6A FF 68 BB 74 83"); // 0x00431F80
        gPatterns.CVehicle__SetRemap             = Resolve("8B 44 24 04 83 F8 FF 56 8B F1 75"); // 0x006D0C00
        gPatterns.CVehicle__AddVehicleUpgrade    = Resolve("53 8B 5C 24 08 56 8B 34 9D"); // 0x006E3290
        gPatterns.CVehicle__RemoveVehicleUpgrade = Resolve("55 8B 6C 24 08 56 8B 34"); // 0x006DF930
        gPatterns.CVehicle__SetupRender          = Resolve("56 8B F1 0F BF 46 22 8B 0D"); // 0x006D64F0
        // mov cl, [esi*4+table]; mov [edx], cl; mov dl, [esi*4+table+1]
        gPatterns.CVehicleModelInfo__ms_vehicleColourTable =
            ResolveOperand("8A 0C B5 ? ? ? ? 88 0A 8A 14 B5", 3); // site 0x004C838D -> 0x00B4E480
        // MTA VAR_CVehicle_Variation1. Adjacent byte is Variation2.
        gPatterns.CVehicleModelInfo__ms_compsToUse =
            ResolveOperand("08 A2 ? ? ? ? 88 0D 59", 2); // site 0x004470A7 -> 0x008A6458
        gPatterns.CModelInfo__ms_modelInfoPtrs =
            ResolveOperand("FF FF 0F BF 4E 22 8B 14 8D ? ? ? ? 8B 42 14 5E", 9); // site 0x005344D2 -> 0x00A9B0C8
        gPatterns.CVehicle__CustomCarPlate_TextureCreate  = Resolve("56 8B 74 24 08 57 8B F9 C7"); // 0x006D10E0
        gPatterns.CVehicle__CustomCarPlate_TextureDestroy = Resolve("56 8B F1 8B 86 88 05"); // 0x006D1150

        // Headlight tint sites. MTA CMultiplayerSA DoHeadLight* hooks.
        gPatterns.CVehicle__DoVehicleLights           = Resolve("A0 18 CC C1 00 83"); // 0x006E1A60
        gPatterns.CVehicle__DoHeadLightBeam           = Resolve("81 EC 94 00 00 00 57 8B F9"); // 0x006E0E20
        gPatterns.CVehicle__DoHeadLightBeam_Verts     = Resolve("C7 05 50 B9 C4 00 05"); // 0x006E13A4
        gPatterns.CVehicle__DoHeadLightEffect_Corona1 = Resolve("E8 ? ? ? ? 83 C4 54 D9 44 24 38"); // 0x006E0D01
        gPatterns.CVehicle__DoHeadLightEffect_Corona2 = Resolve("E8 ? ? ? ? 83 C4 54 8A 44"); // 0x006E0DF7
        gPatterns.CVehicle__DoHeadLightReflectionSingle_Shadow = Resolve("E8 ? ? ? ? 83 C4 30 5E 83 C4 20"); // 0x006E15E2
        gPatterns.CVehicle__DoHeadLightReflectionTwin_Shadow = Resolve("E8 ? ? ? ? 83 C4 4C C2"); // 0x006E170F
        gPatterns.CCoronas__RegisterCorona      = Resolve("0F B6 54 24 28 8B"); // 0x006FC580
        gPatterns.CShadows__StoreCarLightShadow = Resolve("8B 0D 3C F0 B6 00 83 EC 0C"); // 0x0070C500

        gPatterns.CPools__ms_pVehiclePool        = ResolveOperand("8D FA FF 83 C4 08 8B 0D", 8); // site 0x0045FABB -> 0x00B74494

        gPatterns.CTaskComplexEnterCarAsDriver__Constructor    = Resolve("8B 44 24 04 56 6A 00 6A 00 6A 00"); // 0x006402F0
        gPatterns.CTaskComplexEnterCarAsPassenger__Constructor = Resolve("8B 44 24 0C 56 50 6A"); // 0x00640340
        gPatterns.CTaskSimpleCarSetPedInAsDriver__Constructor  = Resolve("6A FF 68 E8 26 84"); // 0x006470E0
        gPatterns.CTaskSimpleCarSetPedInAsPassenger__Constructor = Resolve("6A FF E9 13 AC DB"); // 0x00646FE0
        gPatterns.CTaskComplexLeaveCar__Constructor            = Resolve("6A FF 68 18 16 84"); // 0x0063B8C0
        gPatterns.CTaskSimpleCarSetPedOut__Constructor         = Resolve("6A FF E9 4B A4 DB"); // 0x006478B0
        gPatterns.CTaskSimpleCarFallOut__Constructor           = Resolve("6A FF 68 18 2A 84"); // 0x00648D60
        gPatterns.CCarEnterExit__ComputeTargetDoorToEnterAsPassenger = Resolve("8B 54 24 04 8A 8A"); // 0x0064F190
        gPatterns.CCarEnterExit__ComputeTargetDoorToExit             = Resolve("8B 4C 24 04 8B 91 60"); // 0x0064F110
        gPatterns.CCarEnterExit__ComputePassengerIndexFromCarDoor    = Resolve("8B 44 24 04 83 B8"); // 0x0064F1E0
        gPatterns.CCarEnterExit__GetNearestCarPassengerDoor          = Resolve("83 EC 48 53 56 8B 74"); // 0x00650BB0

        // SEH prologue + m_pDriver at +0x460. The unwind address is build-
        // specific and is masked.
        gPatterns.CVehicle__KillPedsInVehicle = Resolve(
            "64 A1 00 00 00 00 6A FF 68 ? ? ? ? 50 64 89 25 00 00 00 00 83 EC 28 53 56 8B F1 8B 86 60 04 00 00"); // 0x006D1C80
        gPatterns.CVehicle__KillPedsGettingInVehicle = Resolve(
            "6A FF 68 ? ? ? ? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 64 53 89 4C 24 0C 8B 0D"); // 0x006D82F0

        gPatterns.CAutomobile__FixDoor         = Resolve("64 A1 00 00 00 00 6A FF 68 28 7F"); // 0x006A35A0
        gPatterns.CAutomobile__FixPanel        = Resolve("64 A1 00 00 00 00 6A FF 68 48 7F"); // 0x006A3670
        gPatterns.CAutomobile__SetBumperDamage = Resolve("53 55 56 57 8B 7C 24 14 8B F1 57 90"); // 0x006B1350
        gPatterns.CAutomobile__SetPanelDamage  = Resolve("53 55 56 57 8B 7C 24 14 8B F1 57 8D"); // 0x006B1480
        gPatterns.CAutomobile__SetDoorDamage   = Resolve("6A FF 68 78 81 84"); // 0x006B1600

        gPatterns.gFireManager = ResolveOperand("50 B9 ? ? ? ? E8 FD 93", 2); // site 0x0042FB38 -> 0x00B71F80
        gPatterns.CFireManager__StartFire = Resolve("56 57 8B 7C 24 0C 8A 47 36 24 07 3C 03 8B F1 75 1C"); // 0x0053A050
        gPatterns.CFireManager__ExtinguishPoint = Resolve("56 57 8B F1 BF 3C 00 00 00 8D A4 24 00 00 00 00 8A"); // 0x00539450
        gPatterns.CVehicle__ExtinguishCarFire = Resolve("56 8B F1 8A 46 36 24 F8 3C 28"); // 0x006D2460
        gPatterns.CFire__Extinguish = Resolve("56 8B F1 8A 06 A8"); // 0x005393F0
        gPatterns.CExplosion__ClearAllExplosions = Resolve("83 EC 0C 53 55 56 57 C7 44 24 10 00 00 00 00 8B 6C"); // 0x00736840
        gPatterns.CProjectileInfo__RemoveAllProjectiles = Resolve("53 55 56 57 E9 64"); // 0x007399B0

        gPatterns.CStreaming__StreamPedsAndCars  = Resolve("6A 00 6A FF E8 77 2A"); // 0x0040B650
        gPatterns.CStreaming__AmbientModelsJump  = Resolve("75 1A E8 1A CF FF"); // 0x0040E7DF
        gPatterns.CGangWars__Update              = Resolve("83 EC 34 53 E8 37"); // 0x00446610
        gPatterns.CPedType__InterestingEvents    = Resolve("55 8B EC 83 E4 F8 83 EC 28 F6"); // 0x00605A30
        gPatterns.CPed__PlayerConversations      = Resolve("64 A1 00 00 00 00 6A FF 68 BC 76"); // 0x0043B0F0

        Framework::Logging::GetLogger("SDK")->info("Resolved {} patterns", gPatternScanCount);
    }
} // namespace VantaMP::SDK