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
