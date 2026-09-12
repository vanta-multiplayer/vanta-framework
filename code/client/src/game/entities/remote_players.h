#pragma once

#include "sdk/player/slots.h"

namespace VantaMP::Game {
    // kLocalPlayerSlot now lives in sdk/player/slots.h, beside kMaxPlayers -
    // they describe the same GTA table and were split across two layers.

    // Lets the game drive peds that belong to other people.
    //
    // GTA processes exactly one player. Its animation, physics and movement all
    // read the current player's input, camera and info from fixed globals, so a
    // second player ped either does nothing or moves as a copy of the first.
    //
    // Rather than writing animation and physics ourselves - which would mean
    // reimplementing most of the game - remote peds are processed by GTA's own
    // code with the context briefly swapped to theirs. Vanta supplies the input
    // and takes the result.
    void InstallRemotePlayerHooks();

    [[nodiscard]] bool AreRemotePlayerHooksInstalled();

    // Claims a slot for a ped, so the hooks know it is not the local player.
    //
    // Returns false when every slot is taken.
    bool RegisterRemotePlayer(int slot, void *ped);
    void UnregisterRemotePlayer(int slot);

    // Which slot a ped belongs to, or -1 for one Vanta does not own.
    [[nodiscard]] int SlotOfPed(const void *ped);

    [[nodiscard]] void *PedInSlot(int slot);

    // Where a slot's input lives. Write a remote player's controller state here
    // and the game will act on it the next time that ped is processed.
    [[nodiscard]] void *InputForSlot(int slot);

    // Stops remote processing during teardown.
    //
    // The world and the pools are dismantled on the way out, and swapping
    // context against them faults. Set before teardown begins; clear again
    // once the remotes are gone so a later reconnect can process new ones.
    void SuspendRemotePlayers();
    void ResumeRemotePlayers();

    // Called for each remote ped once the game has finished processing it, so
    // a correction lands on top of the movement the game just produced instead
    // of replacing it. See the note at the call site.
    void SetRemotePedCorrector(void (*corrector)(int slot));
} // namespace VantaMP::Game
