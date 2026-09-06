#pragma once

// Reaching into a CPed, for the SDK's own use.
//
// INTERNAL TO src/sdk. This header names plugin-sdk types, so it can only be
// included from a translation unit that CMakeLists has granted the plugin-sdk
// include directories - see VANTAMP_CLIENT_SDK_FILES there. Nothing above the
// SDK boundary may include it.
//
// What lives here is the handful of primitives that both the local player and
// remote peds need. Anything used by only one of them belongs in that file.

#include "game/entities/player_slots.h"
#include "sdk/game_sdk.h"
#include "sdk/patterns.h"

#include <cstddef>
#include <cstdint>

// Layouts this whole boundary is built on. Checked here so every SDK
// translation unit re-checks them, rather than in whichever file happened to
// hold them when the SDK was one file.
static_assert(sizeof(CPlaceable) == 0x18, "CPlaceable layout changed");
static_assert(sizeof(CEntity) == 0x38, "CEntity layout changed");

namespace VantaMP::SDK::Internal {
    // plugin-sdk is header-only here, so it declares the game's functions but
    // their bodies live at 1.0 US addresses in .cpp files this build does not
    // compile. Calling them through resolved addresses keeps the SDK to
    // layouts, which is what it is here for.
    using OperatorNewFn    = void *(__cdecl *)(unsigned int);
    using OperatorDeleteFn = void(__cdecl *)(void *);
    using SetPosnFn        = void(__thiscall *)(void *, float, float, float);
    using GetPadFn         = void *(__cdecl *)(int);

    // CPed's rotation fields, which plugin-sdk does not name.
    //
    // BOTH, NOT EITHER. The game uses m_fCurrentRotation while a ped is on the
    // ground and the matrix while it is in the air, so setting only one leaves a
    // remote player facing the right way until they step off a kerb. MTA writes
    // both for the same reason.
    inline constexpr std::size_t kCurrentRotationOffset = 0x558;
    inline constexpr std::size_t kAimingRotationOffset  = 0x55C;

    inline CPed *LocalPed() {
        // Slot 0, out of the RELOCATED array. Reading gPatterns.CWorld__Players
        // instead would find the array the game shipped with, which nothing
        // writes to any more once the slots have been expanded.
        const auto slot = Game::PlayerInfoAt(0);

        return slot ? *reinterpret_cast<CPed **>(slot) : nullptr;
    }

    // The ped in a slot, or nullptr if that slot is empty.
    inline CPed *PedAt(int slot) {
        auto *info = reinterpret_cast<CPlayerInfo *>(Game::PlayerInfoAt(slot));

        return info ? info->m_pPed : nullptr;
    }

    inline void SetPedPosition(CPed *ped, float x, float y, float z) {
        reinterpret_cast<SetPosnFn>(gPatterns.CPlaceable__SetPosn)(ped, x, y, z);
    }

    inline void SetPedRotation(CPed *ped, float heading) {
        auto *base = reinterpret_cast<std::uint8_t *>(ped);

        *reinterpret_cast<float *>(base + kCurrentRotationOffset) = heading;
        *reinterpret_cast<float *>(base + kAimingRotationOffset)  = heading;
    }

    // READ FROM THE FIELD THAT IS WRITTEN, NOT FROM THE MATRIX.
    //
    // THIS WAS A NINETY DEGREE BUG. Deriving the heading from the matrix as
    // atan2(-right.x, right.y) - which is what CPlaceable::GetHeading does -
    // does NOT return m_fCurrentRotation. The two use different zero
    // directions, and the difference is exactly a quarter turn.
    //
    // That made the read and the write disagree: SetPedRotation writes
    // m_fCurrentRotation, so a heading that went out through here came back
    // ninety degrees rotated. And because the SENDER reports its own facing
    // through this same function, every player announced a heading a quarter
    // turn from where they stood, and every receiver wrote that value straight
    // into the ped's real rotation - so every remote player was drawn ninety
    // degrees off, permanently, with no drift to give it away.
    //
    // Measured: a ped told 78.6 degrees read back as -11.4.
    //
    // MTA has no such asymmetry - it sends GetCurrentRotation and applies
    // SetCurrentRotation, the same field both ways. This does likewise.
    inline float HeadingOf(const CPed *ped) {
        const auto *base = reinterpret_cast<const std::uint8_t *>(ped);

        return *reinterpret_cast<const float *>(base + kCurrentRotationOffset);
    }
} // namespace VantaMP::SDK::Internal
