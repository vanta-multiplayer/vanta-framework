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
        gPatterns.CMenuManager__PauseCheck        = Resolve("FF 53 E8 2C 74 E4"); // 0x00748ADD
        gPatterns.CMenuManager__OnFocusLoss       = Resolve("A0 31 68 BA 00 84 C0 75 10"); // 0x0053BC60
        gPatterns.CMenuManager__AfterAltTabFlag   = Resolve("01 C3 90 90 90 90 90 90 56"); // 0x0053BC78
        gPatterns.Print__AspectRatio              = Resolve("E8 ? ? ? ? D9 44 24 14 D8 1D 34"); // 0x00745997
        gPatterns.Print__SoundManager             = Resolve("E8 ? ? ? ? 8B 4C 24 14 83 C4 04 5F"); // 0x005B97C9

        // Globals, read out of the instruction that references them.
        gPatterns.CTimer__UserPause   = OperandAt(hook::get_pattern("51 A0 ? ? ? ? 84 C0 56 57 8B"), 2); // site 0x004D6DF0 -> 0x00B7CB49
        gPatterns.CMenuManager__Pause = OperandAt(hook::get_pattern("1F 00 A0 ? ? ? ? 84 C0 74"), 3); // site 0x0053E81D -> 0x00BA67A4
        gPatterns.gGameState          = OperandAt(hook::get_pattern("83 3D ? ? ? ? 08 75 63"), 2); // site 0x00730EDD -> 0x00C8D4C0

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
