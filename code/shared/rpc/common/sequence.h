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
    //
    // EIGHT BITS IS ENOUGH, AND THAT IS A MEASURED CLAIM. The sequence exists
    // only to reject a datagram that arrived after a newer one; it does not
    // have to be globally unique. At the 10 Hz send rate a uint8 wraps every
    // 25.6 seconds, and the comparison below treats anything more than 127
    // ticks (12.8 s) away as older. A UDP datagram that is twelve seconds late
    // is not a reorder - it is a packet the transport has long since given up
    // on, and applying it would be wrong anyway.
    //
    // This was uint16, which bought a 109-minute window nothing could use and
    // cost 8 bits on every motion packet.
    using Sequence = std::uint8_t;

    [[nodiscard]] inline bool IsNewerSequence(Sequence incoming, Sequence applied) {
        return static_cast<std::int8_t>(incoming - applied) > 0;
    }
} // namespace VantaMP::Shared::RPC
