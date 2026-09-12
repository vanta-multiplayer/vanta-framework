#pragma once

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // Wrap-safe comparison for the per-entity sequence numbers on every
    // unreliable motion stream.
    //
    // A PLAIN `>` IS WRONG TWICE. It would stall an entity for the best part
    // of two hours at the uint16 wrap point, and it would accept a reordered
    // datagram that arrived after a newer one. Signed difference is the
    // standard comparison and handles both.
    //
    // Shared by the vehicle and player protocols: the rule is identical and
    // two copies would be two chances to get the wrap wrong.
    [[nodiscard]] inline bool IsNewerSequence(std::uint16_t incoming, std::uint16_t applied) {
        return static_cast<std::int16_t>(incoming - applied) > 0;
    }
} // namespace VantaMP::Shared::RPC
