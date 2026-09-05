#include "remote_players.h"

#include "player_slots.h"
#include "sdk/patterns.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

#include <array>
#include <atomic>
#include <cstring>

namespace VantaMP::Game {
    namespace {
        std::array<void *, kMaxPlayers> gOccupants {};

        bool gInstalled = false;
        std::atomic_bool gSuspended {false};

        using ProcessControlFn = void(__thiscall *)(void *);
        using GetPadFn         = void *(__cdecl *)(int);

        ProcessControlFn gOriginalProcessControl = nullptr;

        // What the local player owns, saved for the length of one remote ped's
        // processing and put back afterwards.
        struct SavedContext {
            std::array<std::uint8_t, kPadSize> pad {};
            float cameraRotation = 0.0f;
            bool mouseLook       = false;
            std::int32_t focus   = 0;
        };

        SavedContext gSaved;

        // Guards against re-entry. Nesting a context switch would restore the
        // wrong state on the way out - the inner switch would put back what the
        // outer one had already replaced.
        bool gSwitched = false;

        void *LocalPad() {
            return reinterpret_cast<GetPadFn>(SDK::gPatterns.CPad__GetPad)(0);
        }

        // Makes the game treat `slot`'s player as the current one.
        //
        // THE PAD IS NOT REDIRECTED, ITS CONTENTS ARE REPLACED.
        //
        // The obvious approach is to hook CPad::GetPad and hand back a different
        // pad per slot. The previous client tried that: it recursed, because the
        // hook called a helper that called GetPad, and it broke startup outright.
        // More fundamentally it fights the game instead of working with it -
        // every call site has to be found and every one has to agree.
        //
        // So nothing is redirected. The local pad's contents are saved, the
        // remote player's input is copied into that same pad, and every existing
        // GetPad call in the game reads the right memory unmodified. Only the
        // contents change, and only for the duration of one call.
        //
        // Camera rotation is swapped for the same reason: the game derives which
        // way "forward" is from it, so without this a remote player walks
        // relative to the LOCAL camera and changes direction as the local player
        // turns their head.
        void SwitchToRemote(int slot) {
            const auto localPad = LocalPad();

            if (!localPad) {
                return;
            }

            const auto &p = SDK::gPatterns;

            std::memcpy(gSaved.pad.data(), localPad, kPadSize);
            gSaved.cameraRotation = *reinterpret_cast<float *>(p.CCamera__Rotation);
            gSaved.mouseLook      = *reinterpret_cast<bool *>(p.CCamera__MouseLook);
            gSaved.focus          = *reinterpret_cast<std::int32_t *>(p.CWorld__PlayerInFocus);

            if (const auto remotePad = PadAt(slot)) {
                std::memcpy(localPad, remotePad, kPadSize);
            }

            *reinterpret_cast<std::int32_t *>(p.CWorld__PlayerInFocus) = slot;

            // Local mouse movement must not steer somebody else's player.
            *reinterpret_cast<bool *>(p.CCamera__MouseLook) = false;
        }

        // Undoes SwitchToRemote, keeping whatever the game wrote.
        void SwitchToLocal(int slot) {
            const auto localPad = LocalPad();

            if (!localPad) {
                return;
            }

            const auto &p = SDK::gPatterns;

            // COPIED BACK, NOT DISCARDED. The game writes into the pad while it
            // processes - button histories, hold timings - and that is state the
            // remote player needs to keep. Throwing it away resets their input
            // every frame, which shows up as movement that stutters rather than
            // as anything obviously broken.
            if (const auto remotePad = PadAt(slot)) {
                std::memcpy(remotePad, localPad, kPadSize);
            }

            std::memcpy(localPad, gSaved.pad.data(), kPadSize);

            *reinterpret_cast<float *>(p.CCamera__Rotation)            = gSaved.cameraRotation;
            *reinterpret_cast<bool *>(p.CCamera__MouseLook)            = gSaved.mouseLook;
            *reinterpret_cast<std::int32_t *>(p.CWorld__PlayerInFocus) = gSaved.focus;
        }

        // Replaces CPlayerPed::ProcessControl.
        //
        // Remote peds run the game's own processing under a swapped context.
        // Vanta writes no animation or physics of its own.
        void __fastcall ProcessControl_Hook(void *ped, void * /*edx*/) {
            if (!ped) {
                return;
            }

            // Once teardown has begun the world and the pools are being
            // dismantled, and a context switch against them faults.
            if (gSuspended.load(std::memory_order_acquire)) {
                gOriginalProcessControl(ped);
                return;
            }

            const int slot = SlotOfPed(ped);

            // The local player, a ped Vanta does not own, or a nested call: all
            // take the ordinary path.
            if (slot < 0 || slot == kLocalPlayerSlot || gSwitched) {
                gOriginalProcessControl(ped);
                return;
            }

            gSwitched = true;
            SwitchToRemote(slot);

            gOriginalProcessControl(ped);

            SwitchToLocal(slot);
            gSwitched = false;
        }

        // Replaces CPlayerPed::GetPlayerInfoForThisPlayerPed.
        //
        // The stock version assumes there is one player and returns its info
        // unconditionally. With several player peds it has to find the one that
        // belongs to this ped, or every remote player inherits the local
        // player's health, weapons and wanted level.
        void *__fastcall GetPlayerInfo_Hook(void *ped, void * /*edx*/) {
            if (ped) {
                if (const int slot = SlotOfPed(ped); slot >= 0) {
                    return PlayerInfoAt(slot);
                }
            }

            // The local player's info is the safe answer: it is what the stock
            // function would have returned.
            return PlayerInfoAt(kLocalPlayerSlot);
        }
    } // namespace

    void InstallRemotePlayerHooks() {
        if (gInstalled) {
            return;
        }

        gInstalled = true;

        const auto &p = SDK::gPatterns;

        gOriginalProcessControl = reinterpret_cast<ProcessControlFn>(p.CPlayerPed__ProcessControl);

        // REPLACED IN THE VTABLE, NOT AT THE FUNCTION.
        //
        // ProcessControl is reached through CPlayerPed's vtable, so overwriting
        // the entry redirects every call without touching the function body -
        // and leaves the original intact to call afterwards.
        hook::put<void *>(p.CPlayerPed__ProcessControlVtable, reinterpret_cast<void *>(&ProcessControl_Hook));

        hook::jump(p.CPlayerPed__GetPlayerInfo, &GetPlayerInfo_Hook);

        // NOTE: CPad::GetPad is deliberately NOT hooked. See SwitchToRemote -
        // the context switch puts the remote player's input into the pad the
        // game already reads, so every existing call site resolves correctly
        // with no redirection. Hooking it recursed and broke startup.

        Framework::Logging::GetLogger("Players")->info("Remote player hooks installed; up to {} players can be processed", kMaxPlayers);
    }

    bool AreRemotePlayerHooksInstalled() {
        return gInstalled;
    }

    bool RegisterRemotePlayer(int slot, void *ped) {
        if (slot < 0 || slot >= kMaxPlayers || !ped) {
            return false;
        }

        gOccupants[static_cast<std::size_t>(slot)] = ped;
        return true;
    }

    void UnregisterRemotePlayer(int slot) {
        if (slot < 0 || slot >= kMaxPlayers) {
            return;
        }

        gOccupants[static_cast<std::size_t>(slot)] = nullptr;
    }

    int SlotOfPed(const void *ped) {
        if (!ped) {
            return -1;
        }

        for (int slot = 0; slot < kMaxPlayers; ++slot) {
            if (gOccupants[static_cast<std::size_t>(slot)] == ped) {
                return slot;
            }
        }

        return -1;
    }

    void *PedInSlot(int slot) {
        if (slot < 0 || slot >= kMaxPlayers) {
            return nullptr;
        }

        return gOccupants[static_cast<std::size_t>(slot)];
    }

    void *InputForSlot(int slot) {
        return PadAt(slot);
    }

    void SuspendRemotePlayers() {
        gSuspended.store(true, std::memory_order_release);
    }
} // namespace VantaMP::Game
