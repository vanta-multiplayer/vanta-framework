#include "sdk/player/slots.h"

#include "sdk/patterns.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

#include <cstring>

namespace VantaMP::SDK {
    namespace {
        // Static storage rather than the heap: these have to outlive everything
        // in the game, and a fixed address makes the rewriting below simple to
        // reason about and to verify afterwards.
        alignas(16) std::uint8_t gPlayerInfo[kMaxPlayers * kPlayerInfoSize] {};
        alignas(16) std::uint8_t gPads[kMaxPlayers * kPadSize] {};

        bool gExpanded = false;

        struct CodeRange {
            std::uintptr_t start = 0;
            std::uintptr_t end   = 0;
        };

        // The executable sections, each on its own.
        //
        // ONE RANGE PER SECTION, NEVER A SPAN ACROSS THEM. This image has six
        // executable sections scattered from 0x401000 to 0x1577000, because its
        // protector marks its own data executable - so the lowest start with the
        // highest end covers 17.5 MB of which most is not code at all. Rewriting
        // four-byte values across that corrupts live data, and the game dies
        // somewhere unrelated with a stack that says nothing about why.
        //
        // Read from the headers rather than hardcoded: the previous client
        // carried the end of .text as a literal, which is a fact about one build
        // and silently wrong for any other.
        template <typename Visitor>
        void ForEachCodeRange(Visitor &&visit) {
            const auto module = reinterpret_cast<const std::uint8_t *>(GetModuleHandleW(nullptr));
            const auto *dos   = reinterpret_cast<const IMAGE_DOS_HEADER *>(module);
            const auto *nt    = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(module + dos->e_lfanew);

            const auto *section = IMAGE_FIRST_SECTION(nt);

            for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
                if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
                    continue;
                }

                // Writable sections are data the protector happens to have
                // marked executable. Real code is not writable, and the
                // difference is what keeps this off .data.
                if ((section->Characteristics & IMAGE_SCN_MEM_WRITE) != 0) {
                    continue;
                }

                const auto begin = reinterpret_cast<std::uintptr_t>(module) + section->VirtualAddress;

                visit(CodeRange {begin, begin + section->Misc.VirtualSize});
            }
        }

        // Rewrites one four-byte value if it refers to a relocated array.
        //
        // Two cases, and the second is the one that is easy to forget:
        //
        //   the value IS the array address        -> point it at the new array
        //   the value points INTO the first entry -> keep the field offset
        //
        // The game addresses fields directly all the time - `mov eax,
        // [0xB7CD98 + 0x2C]` - so handling only exact matches leaves most of
        // the references behind.
        bool RewriteReference(std::uintptr_t address, std::uintptr_t original, std::size_t structureSize, const void *replacement) {
            const auto value = *reinterpret_cast<const std::uint32_t *>(address);

            if (value == original) {
                hook::putVP<std::uint32_t>(address, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(replacement)));
                return true;
            }

            if (value > original && value <= original + structureSize) {
                const auto offset = value - original;
                hook::putVP<std::uint32_t>(address, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(replacement) + offset));
                return true;
            }

            return false;
        }
    } // namespace

    void ExpandPlayerSlots() {
        if (gExpanded) {
            return;
        }

        gExpanded = true;

        const auto &p = SDK::gPatterns;

        // 1. How many the game allocates for itself.
        //
        // CLAMPED TO 255, WHICH LOOKS WRONG AND IS NOT. These are `push imm8`
        // instructions, so the operand is a single byte and 260 would truncate
        // to 4. It does not matter: the count only sizes the game's OWN
        // allocation, which is replaced below by storage sized from
        // kMaxPlayers. What the game allocates here is never used.
        constexpr std::uint8_t kAllocationCount = 255;

        hook::put<std::uint8_t>(p.Alloc__PlayerInfo1 + 1, kAllocationCount);
        hook::put<std::uint8_t>(p.Alloc__PlayerInfo2 + 1, kAllocationCount);
        hook::put<std::uint8_t>(p.Alloc__Pad1 + 1, kAllocationCount);
        hook::put<std::uint8_t>(p.Alloc__Pad2 + 1, kAllocationCount);

        // 2. Every reference, in each code section separately.
        const auto started = GetTickCount64();

        const auto playerInfoOriginal = p.CWorld__Players;
        const auto padOriginal        = p.CPad__Pads;

        int rewritten = 0;

        ForEachCodeRange([&](const CodeRange &code) {
            DWORD previous = 0;
            VirtualProtect(reinterpret_cast<void *>(code.start), code.end - code.start, PAGE_EXECUTE_READWRITE, &previous);

            for (auto address = code.start; address + 4 <= code.end; ++address) {
                if (RewriteReference(address, playerInfoOriginal, kPlayerInfoSize, gPlayerInfo)) {
                    ++rewritten;
                    address += 3;
                    continue;
                }

                // A CALL's relative displacement can coincidentally equal an
                // array address. Skipping the byte that starts one avoids
                // rewriting the displacement and sending the call elsewhere.
                if (*reinterpret_cast<const std::uint8_t *>(address) == 0xE8) {
                    continue;
                }

                if (RewriteReference(address, padOriginal, kPadSize, gPads)) {
                    ++rewritten;
                    address += 3;
                }
            }

            VirtualProtect(reinterpret_cast<void *>(code.start), code.end - code.start, previous, &previous);
        });
    }

    bool ArePlayerSlotsExpanded() {
        return gExpanded;
    }

    void *PlayerInfoAt(int slot) {
        if (slot < 0 || slot >= kMaxPlayers) {
            return nullptr;
        }

        return gPlayerInfo + static_cast<std::size_t>(slot) * kPlayerInfoSize;
    }

    void *PadAt(int slot) {
        if (slot < 0 || slot >= kMaxPlayers) {
            return nullptr;
        }

        return gPads + static_cast<std::size_t>(slot) * kPadSize;
    }
} // namespace VantaMP::SDK
