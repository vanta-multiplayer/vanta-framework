#include "aim.h"

#include "game/entities/player_slots.h"
#include "game/entities/remote_players.h"
#include "sdk/patterns.h"

#include <utils/hooking/hooking.h>

#include <array>
#include <cstddef>
#include <cstdint>

// No plugin-sdk here, deliberately. Everything this needs is a pointer, an
// offset and a slot number, so it stays off the list in CMakeLists.

namespace VantaMP::SDK {
    namespace {
        // Where CPedIK sits inside a CPed.
        //
        // The intercept is handed the CPedIK, not the ped, and the ped is what
        // says whose aim this is - so it walks back by the member's offset.
        // plugin-sdk asserts this same number (VALIDATE_OFFSET(CPed, m_pedIK,
        // 0x50C)), and it is the constant MTA subtracts as 1292.
        constexpr std::ptrdiff_t kPedIkOffset = 0x50C;

        std::array<PedAim, static_cast<std::size_t>(Game::kMaxPlayers)> gRemoteAim {};

        PedAim gLocalAim {};

        using PointGunFn = bool(__fastcall *)(void *, void *, float, float, bool, float);

        // The game aiming a ped's gun, with the answer corrected on the way past.
        //
        // __fastcall with a spare second parameter is how a __thiscall is
        // expressed: the object arrives in ecx, edx is unused, and the declared
        // arguments follow on the stack exactly as the game pushed them.
        bool __fastcall PointGunInDirection_Hook(void *pedIK, void *unused, float zAngle, float pitch, bool flag, float limit) {
            (void)unused;

            const auto *ped = reinterpret_cast<const std::uint8_t *>(pedIK) - kPedIkOffset;

            const int slot = Game::SlotOfPed(ped);

            if (slot == Game::kLocalPlayerSlot) {
                // TAKEN ON THE WAY THROUGH, NOT CHANGED. This is the one ped
                // whose aim the game is computing correctly, and the only chance
                // to see the numbers before they are used and forgotten.
                gLocalAim.horizontal = zAngle;
                gLocalAim.vertical   = pitch;
            } else if (slot > Game::kLocalPlayerSlot) {
                const auto &aim = gRemoteAim[static_cast<std::size_t>(slot)];

                zAngle = aim.horizontal;
                pitch  = aim.vertical;
            }

            // A ped in no slot of ours - traffic, a pedestrian - is left alone.
            return reinterpret_cast<PointGunFn>(gPatterns.CPedIK__PointGunInDirection)(pedIK, unused, zAngle, pitch, flag, limit);
        }
    } // namespace

    PedAim ReadLocalPedAim() {
        return gLocalAim;
    }

    void SetRemotePedAim(int slot, float horizontal, float vertical) {
        if (slot < 0 || slot >= Game::kMaxPlayers) {
            return;
        }

        gRemoteAim[static_cast<std::size_t>(slot)] = {horizontal, vertical};
    }

    PedAim GetRemotePedAim(int slot) {
        if (slot < 0 || slot >= Game::kMaxPlayers) {
            return {};
        }

        return gRemoteAim[static_cast<std::size_t>(slot)];
    }

    void InstallAimSync() {
        const auto &p = gPatterns;

        // THE CALL SITES, NOT THE FUNCTION. Redirecting each caller leaves the
        // function itself untouched, so the original is still there to be called
        // with corrected arguments - no prologue to copy and no trampoline to
        // get wrong. There are only three, which is what makes this the cheaper
        // option here.
        hook::call(p.CPedIK__PointGunInDirectionCallA, PointGunInDirection_Hook);
        hook::call(p.CPedIK__PointGunInDirectionCallB, PointGunInDirection_Hook);
        hook::call(p.CPedIK__PointGunInDirectionCallC, PointGunInDirection_Hook);
    }
} // namespace VantaMP::SDK
