#pragma once

#include <cstddef>
#include <cstdint>

namespace VantaMP::SDK {
    // GTA'S PLAYER SLOT TABLE. This is memory-layout knowledge about the
    // executable - patched array bases, structure sizes verified against the
    // game's own allocation sites - so it belongs in the SDK layer, not above
    // it. It lived under game/entities/ and was included by seven SDK files,
    // which made the SDK depend upwards on game code; moving the file is the
    // whole fix.

    // How many players the client can hold at once.
    //
    // Slot 0 is the local player. Slot 1 is deliberately left alone - it is a
    // leftover from the game's two-player co-op mode and behaves differently
    // from the rest - so remote players start at slot 2.
    inline constexpr int kMaxPlayers = 260;

    // Declared beside kMaxPlayers because the two describe the same table.
    // It used to live in game/entities/remote_players.h, one layer up.
    inline constexpr int kLocalPlayerSlot = 0;

    // Replaces GTA's two-entry player arrays with ones big enough for a server.
    //
    // San Andreas allocates room for two players, which is what co-op needed.
    // Everything downstream - CWorld::Players, the pad array - is sized from
    // that, so a third player has nowhere to exist.
    //
    // The arrays are relocated into storage of our own and every reference to
    // the originals in the game's code section is rewritten to match. That is a
    // blunt instrument, and it is the only correct one available: the references
    // are scattered immediates compiled into individual instructions, not a
    // table that could be repointed in one place.
    //
    // Runs once, before the game starts. It scans roughly 4.5 MB byte by byte
    // and takes a noticeable moment; there is no cheaper way to find every
    // `mov eax, [0xB7CD98 + 0x2C]` the compiler emitted.
    void ExpandPlayerSlots();

    [[nodiscard]] bool ArePlayerSlotsExpanded();

    // The relocated entries. Untyped on purpose: the layouts belong to
    // plugin-sdk, which is confined to one translation unit, and nothing here
    // needs to look inside them - only to know where they are and how big.
    [[nodiscard]] void *PlayerInfoAt(int slot);
    [[nodiscard]] void *PadAt(int slot);

    // WHICH PED OCCUPIES WHICH SLOT.
    //
    // An index over the same patched player array this header owns, so it
    // belongs here rather than one layer up - three SDK files were reaching
    // into game/entities/ for exactly these four calls.
    //
    // Register returns false when the slot is out of range or the ped is null.
    bool RegisterRemotePlayer(int slot, void *ped);
    void UnregisterRemotePlayer(int slot);

    // Which slot a ped belongs to, or -1 for one Vanta does not own.
    [[nodiscard]] int SlotOfPed(const void *ped);

    [[nodiscard]] void *PedInSlot(int slot);

    // Sizes, verified against the executable's own allocation sites: both push
    // the structure size as an immediate right after the count.
    inline constexpr std::size_t kPlayerInfoSize = 0x190;
    inline constexpr std::size_t kPadSize        = 0x134;
} // namespace VantaMP::SDK
