#include "patterns.h"

#include <utils/hooking/hooking.h>
#include <utils/hooking/hooking_patterns.h>

namespace VantaMP::SDK {
    Patterns gPatterns;

    void Patterns::InitPatterns() {
        // A bare global with no accessor, so it comes out of a site that indexes it: imul by
        // sizeof(CPlayerInfo), load the ped at offset 0. The trailing flag test keeps it unique.
        const auto playersSite    = hook::get_pattern<uint8_t>("69 C0 90 01 00 00 8B 80 ? ? ? ? 83 EC 18 F7 80 6C 04 00 00 00 01 00 00");
        gPatterns.CWorld__Players = *reinterpret_cast<const uint32_t *>(playersSite + 8);
    }
} // namespace VantaMP::SDK
