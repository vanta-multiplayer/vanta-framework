#pragma once

#include <cstdint>

namespace VantaMP::Shared::RPC::Common {
    // A compact, session-local reference to a networked entity.
    //
    // SHARED BY VEHICLES AND PLAYERS. There is one wire-id scheme in this
    // project, not one per entity kind: the lifetime and reuse problem is
    // identical for both, and two implementations would be two chances to
    // get the generation handling wrong.
    //
    // THE CANONICAL ID DOES NOT CHANGE. NetworkID stays the 64-bit value
    // ReplicaManager3 assigns and every RPC outside the hot path keeps
    // using it. This is only a hot-path ADDRESSING form: at 100 ms a
    // 64-bit id is the single largest field in a motion packet - bigger
    // than the quantized rotation it sits next to.
    //
    // STALE IDS MUST NOT ADDRESS A REUSED SLOT. That is the whole risk of a
    // compact id, and the reason for the generation half: a slot freed and
    // handed to a different vehicle comes back with a different generation,
    // so a motion packet still in flight for the old occupant fails to
    // resolve instead of silently steering its replacement. Without this a
    // car that despawned a tick ago would move a newly spawned one.
    //
    //   bits 0-11   slot       (4096 concurrent networked vehicles)
    //   bits 12-15  generation (16 reuses before a slot repeats)
    //
    // 4096 is far above what the game can hold: GTA's vehicle pool tops out
    // near 110 streamed in and the ped pool is the same order. The headroom
    // is for server-side entities that exist while nobody is near them.
    //
    // Each entity kind keeps its OWN registry instance, so a slot number
    // means nothing across kinds - a player and a vehicle may both hold
    // slot 7. Packets are already typed by their RPC identifier, so there
    // is nothing to disambiguate on the wire.
    struct WireId {
        static constexpr int           kSlotBits       = 12;
        static constexpr int           kGenerationBits = 4;
        static constexpr int           kBits           = kSlotBits + kGenerationBits;
        static constexpr std::uint16_t kSlotMask       = (1u << kSlotBits) - 1u;
        static constexpr std::uint16_t kGenerationMask = (1u << kGenerationBits) - 1u;
        static constexpr std::uint16_t kMaxSlots       = 1u << kSlotBits;
        static constexpr std::uint16_t kInvalid        = 0xFFFF;

        std::uint16_t value = kInvalid;

        [[nodiscard]] static constexpr WireId Make(std::uint16_t slot, std::uint8_t generation) {
            return WireId {static_cast<std::uint16_t>((slot & kSlotMask) | ((generation & kGenerationMask) << kSlotBits))};
        }

        [[nodiscard]] constexpr std::uint16_t Slot() const {
            return value & kSlotMask;
        }

        [[nodiscard]] constexpr std::uint8_t Generation() const {
            return static_cast<std::uint8_t>((value >> kSlotBits) & kGenerationMask);
        }

        [[nodiscard]] constexpr bool Valid() const {
            return value != kInvalid;
        }

        [[nodiscard]] constexpr bool operator==(const WireId &other) const {
            return value == other.value;
        }
    };
} // namespace VantaMP::Shared::RPC::Common
