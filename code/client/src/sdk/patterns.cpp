#include "patterns.h"

#include <utils/hooking/hooking.h>
#include <utils/hooking/hooking_patterns.h>

#include <string_view>

namespace VantaMP::SDK {
    Patterns gPatterns;

    namespace {
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
            return reinterpret_cast<uintptr_t>(hook::get_pattern(signature));
        }
    } // namespace

    void Patterns::InitPatterns() {
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
        gPatterns.CWorld__Players = OperandAt(hook::get_pattern("C7 69 C0 90 01 00 00 8B 80 ? ? ? ? 3B C3 74"), 9); // site 0x00570D05 -> 0x00B7CD98

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
        gPatterns.CTimer__UserPause   = OperandAt(hook::get_pattern("51 A0 ? ? ? ? 84 C0 56 57 8B"), 2); // site 0x004D6DF0 -> 0x00B7CB49
        gPatterns.CMenuManager__Pause = OperandAt(hook::get_pattern("1F 00 A0 ? ? ? ? 84 C0 74"), 3); // site 0x0053E81D -> 0x00BA67A4
        gPatterns.gGameState          = OperandAt(hook::get_pattern("83 3D ? ? ? ? 08 75 63"), 2); // site 0x00730EDD -> 0x00C8D4C0
        gPatterns.RsGlobal            = OperandAt(hook::get_pattern("C7 05 ? ? ? ? 88 D3"), 2); // site 0x00619602 -> 0x00C17040

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
        gPatterns.CCamera__Fade                     = Resolve("E9 ? ? ? ? 53 8A 59"); // 0x0050AC20
        gPatterns.CCamera__SetFadeColour            = Resolve("8A 44 24 04 3C 02"); // 0x0050BF00

        gPatterns.CWorld__PlayerInFocus = OperandAt(hook::get_pattern("3B F8 75 19 0F B6 0D"), 7); // site 0x004B5C58 -> 0x00B7CD74
        gPatterns.TheCamera             = OperandAt(hook::get_pattern("50 56 B9 ? ? ? ? C6"), 3); // site 0x00520D82 -> 0x00B6F028

        // --- Input -----------------------------------------------------------

        gPatterns.DirectInput__MouseExclusiveMode = Resolve("8A 44 24 04 8B 0D"); // 0x007469A0
        gPatterns.DirectInput__MouseReinit1       = Resolve("74 0F E8 9D 02 1D"); // 0x00576CCC
        gPatterns.DirectInput__MouseReinit2       = Resolve("74 0E E8 AF 00 1D"); // 0x00576EBA
        gPatterns.DirectInput__MouseReinit3       = Resolve("74 0E E8 DF FF 1C"); // 0x00576F8A
        gPatterns.Cursor__Recentre                = Resolve("83 EC 10 56 8B 74 24 18 D9"); // 0x007453F0
        gPatterns.Cursor__Hiding                  = Resolve("6A 00 FF 15 EC 82 85 00 6A"); // 0x007481CD

        gPatterns.IsForegroundApp = OperandAt(hook::get_pattern("A1 1C 62 8D 00 85 C0 0F"), 1); // site 0x00746060 -> 0x008D621C

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

        gPatterns.CDraw__AspectRatio = OperandAt(hook::get_pattern("8B 0D ? ? ? ? D8 0D D4"), 2); // site 0x0053EB26 -> 0x00C3EFA4

        // --- Player slots ------------------------------------------------------
        //
        // Each of these signatures carries the array it allocates for, as the
        // size and address the game pushes right after the count - so they
        // document what they are without a separate comment.
        gPatterns.Alloc__PlayerInfo1 = Resolve("6A 02 68 90 01 00 00 68 98 CD B7 00 E8 95"); // 0x0084E98A
        gPatterns.Alloc__PlayerInfo2 = Resolve("6A 02 68 90 01 00 00 68 98 CD B7 00 E8 EC"); // 0x00856505
        gPatterns.Alloc__Pad1        = Resolve("6A 02 68 34 01 00 00 68 58 34 B7 00 E8 25"); // 0x0084E1FA
        gPatterns.Alloc__Pad2        = Resolve("6A 02 68 34 01 00 00 68 58 34 B7 00 E8 8C"); // 0x00856465

        gPatterns.CPad__Pads = OperandAt(hook::get_pattern("00 B9 ? ? ? ? E9 C0"), 2); // site 0x0040380C -> 0x00B73458

        // --- Remote players ----------------------------------------------------

        gPatterns.CPlayerPed__ProcessControl       = Resolve("83 EC 34 53 55 56 8B F1 8B"); // 0x0060EA90
        gPatterns.CPlayerPed__ProcessControlVtable = Resolve("90 EA 60 00 B0 DF"); // 0x0086D190
        gPatterns.CPlayerPed__GetPlayerInfo        = Resolve("33 C0 BA 98 CD B7"); // 0x00609FF0
        gPatterns.CPad__GetPad                     = Resolve("8B 44 24 04 69 C0 34"); // 0x0053FB70
        gPatterns.CPad__Clear                      = Resolve("56 8B F1 E8 08 D5"); // 0x00541A70
        gPatterns.CPad__ClearMouseHistory          = Resolve("32 C0 A2 04 34 B7"); // 0x00541BD0

        gPatterns.CPad__NewMouseState = OperandAt(hook::get_pattern("FF A0 ? ? ? ? 83 C4 0C 3A"), 2); // site 0x0057E51E -> 0x00B73418

        // Tasks allocate from their own pool, not the ped one - the signature
        // differs from CPed::operator new only in the pool it loads.
        gPatterns.CTask__operator_new            = Resolve("8B 0D A8 44 B7 00 E9"); // 0x0061A5A0
        gPatterns.CTaskSimpleDuck__Constructor   = Resolve("56 8B F1 E8 C8 83"); // 0x00691FC0
        gPatterns.CTaskManager__SetTaskSecondary = Resolve("53 8B 5C 24 08 56 57 8B 7C 24 14 8B F1 8B"); // 0x00681B60
        gPatterns.CTaskManager__GetTaskSecondary = Resolve("8B 44 24 04 8B 44 81 14"); // 0x00681810

        // Near-identical prologues, so both carry the call that follows: the
        // duck constructor above starts with the same four bytes and is told
        // apart only by its relative offset.
        gPatterns.CTaskComplexInWater__Constructor = Resolve("56 8B F1 E8 D8 52"); // 0x006350D0
        gPatterns.CTaskManager__SetTask            = Resolve("53 8B 5C 24 08 85 DB 56 57 8B 7C"); // 0x00681AF0

        gPatterns.CCamera__Rotation  = OperandAt(hook::get_pattern("FF D8 25 ? ? ? ? D9 5C 24 20"), 3); // site 0x00688FA6 -> 0x00B6F178
        gPatterns.CCamera__MouseLook = OperandAt(hook::get_pattern("50 A0 ? ? ? ? 64 89 25 00 00 00 00 83 EC 74"), 2); // site 0x0060B65D -> 0x00B6EC2E

        // --- Game methods called directly --------------------------------------

        gPatterns.CPed__operator_new                 = Resolve("8B 0D 90 44 B7 00 E9"); // 0x005E4720

        // Its matching delete has no unique signature of its own - the body is
        // too short and too ordinary - so it is reached through a call to it,
        // which get_opcode_address follows to the target.
        gPatterns.CPed__operator_delete = static_cast<uintptr_t>(hook::get_opcode_address("E8 ? ? ? ? 83 C4 04 8B C6 5E C2 04 00 90 90 83 EC 20 53 55 56 57")); // call at 0x0060A9B0 -> 0x005E4760
        gPatterns.CPlayerPed__Constructor            = Resolve("6A FF 68 58 ED 83"); // 0x0060D5B0
        gPatterns.CPed__SetModelIndex                = Resolve("56 8B F1 81 4E 1C"); // 0x005E4880
        gPatterns.CPlaceable__SetOrientation         = Resolve("83 EC 0C 56 8B F1 8B 4E 14"); // 0x00439A80
        gPatterns.CPedIntelligence__FlushImmediately = Resolve("6A FF 68 26 E8 83"); // 0x00601640
        gPatterns.CPedClothesDesc__Constructor       = Resolve("8B D1 57 33 C0 B9 0A 00 00 00 8B FA F3 AB 8D 7A 28 B9 12 00 00 00 F3 AB 89 42 70 89 42 74 8B"); // 0x005A8020
        gPatterns.CPedClothesDesc__Initialise        = Resolve("8B D1 57 33 C0 B9 0A 00 00 00 8B FA F3 AB 8D 7A 28 B9 12 00 00 00 F3 AB 89 42 70 89 42 74 5F"); // 0x005A78F0
        gPatterns.CWorld__Add                        = Resolve("56 8B 74 24 08 8B 46 18 85 C0 74"); // 0x00563220
        gPatterns.CWorld__Remove                     = Resolve("56 8B 74 24 08 8B 06 8B CE FF 50 0C"); // 0x00563280

        gPatterns.CPed__GiveWeapon       = Resolve("51 53 8B 5C 24 0C 55 56 57"); // 0x005E6080
        gPatterns.CPed__SetCurrentWeapon = Resolve("53 8B 5C 24 08 83 FB FF 56 8B"); // 0x005E61F0
        gPatterns.CPed__ClearWeapon      = Resolve("56 57 8B 7C 24 0C 6A 01 57 8B F1 E8 A0"); // 0x005E62B0

        // --- Streaming ----------------------------------------------------------

        gPatterns.CStreaming__RequestModel           = Resolve("53 8B 5C 24 0C 55 8B 6C 24 0C 56 57 8D"); // 0x004087E0
        gPatterns.CStreaming__LoadAllRequestedModels = Resolve("E9 ? ? ? ? 56 33 F6 84 C0 0F"); // 0x0040EA10
        gPatterns.CStreaming__HasModelLoaded         = Resolve("8B 44 24 04 8D 04 80 8A 14 85 D0"); // 0x004044C0

        gPatterns.CWeaponInfo__GetWeaponInfo = Resolve("8A 4C 24 08 84 C9 E9"); // 0x00743C60

        // --- Running more than one client --------------------------------------

        gPatterns.MultiInstanceCheckCall = Resolve("E8 ? ? ? ? 85 C0 75 0F 53"); // 0x0074872D
        gPatterns.IsAlreadyRunning       = Resolve("A1 24 62 8D 00 50"); // 0x007468E0
        gPatterns.CdStreamSemaphoreName  = OperandAt(hook::get_pattern("00 68 ? ? ? ? A3 EC"), 2); // site 0x00406944 -> 0x00858AD4

        // --- Population ------------------------------------------------------

        gPatterns.CPopulation__Add               = Resolve("6A FF 68 8E EF 83"); // 0x00614720
        gPatterns.CPopulation__ManagePed         = Resolve("51 56 8B 74 24 0C 8B CE E8 23"); // 0x00611FC0
        gPatterns.CPopulation__RemovePed         = Resolve("56 8B 74 24 08 56 E8 55 23"); // 0x00610F20
        gPatterns.CPopulation__UpdateEarlyOut    = Resolve("E8 ? ? ? ? A0 23"); // 0x00616698
        gPatterns.CCarCtrl__GenerateRandomCars   = Resolve("A0 51 F8 B5 00 84 C0 74 08"); // 0x004341C0
        gPatterns.CCarCtrl__RemoveCarsIfPoolFull = Resolve("A1 4C CB B7 00 83 E0 07 83 EC 08 3C 03"); // 0x004322B0
        gPatterns.CStreaming__StreamPedsAndCars  = Resolve("6A 00 6A FF E8 77 2A"); // 0x0040B650
        gPatterns.CStreaming__AmbientModelsJump  = Resolve("75 1A E8 1A CF FF"); // 0x0040E7DF
        gPatterns.CGangWars__Update              = Resolve("83 EC 34 53 E8 37"); // 0x00446610
        gPatterns.CPedType__InterestingEvents    = Resolve("55 8B EC 83 E4 F8 83 EC 28 F6"); // 0x00605A30
        gPatterns.CPed__PlayerConversations      = Resolve("64 A1 00 00 00 00 6A FF 68 BC 76"); // 0x0043B0F0
    }
} // namespace VantaMP::SDK
