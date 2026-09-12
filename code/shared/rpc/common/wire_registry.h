#pragma once

#include "../common/wire_id.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace VantaMP::Shared::RPC::Common {
    // Maps the canonical 64-bit NetworkID to the compact hot-path WireId and
    // back.
    //
    // THE SERVER OWNS THIS MAPPING. Clients never invent a wire id; they
    // learn it from the packets that carry both forms (spawn, resync,
    // sync-owner) and use it only to address motion. That keeps one
    // authority over the slot space, which is what makes reuse safe to
    // reason about.
    //
    // REUSE IS THE WHOLE PROBLEM. A slot handed out again to a different
    // vehicle must not be addressable by a motion packet still in flight for
    // the previous occupant, or a despawned car steers its replacement. The
    // generation counter in WireId is what prevents that: Release() bumps the
    // slot's generation, so every id minted before the release fails to
    // resolve afterwards.
    //
    // Slots are reused OLDEST-FIRST rather than immediately, so a stale
    // packet has to survive a full lap of the free list before it can even
    // reach the generation check. Belt and braces, and free.
    class WireRegistry final {
      public:
        // SIZED AT CONSTRUCTION, NOT LAZILY.
        //
        // These tables used to be filled by EnsureSized(), which was only ever
        // reached through TakeSlot() -> Acquire(). The server calls Acquire
        // because it mints ids; a CLIENT only ever calls Adopt() and
        // Resolve(), so its tables stayed EMPTY and the first inbound packet
        // indexed _slotLive[slot] out of bounds. That is an access violation
        // on the first motion packet after connecting.
        WireRegistry() {
            _slotNetworkId.assign(WireId::kMaxSlots, 0);
            _generation.assign(WireId::kMaxSlots, 0);
            _slotLive.assign(WireId::kMaxSlots, false);
        }

        // Assigns a wire id to a network id, or returns the existing one.
        [[nodiscard]] WireId Acquire(std::uint64_t networkId) {
            if (const auto it = _byNetworkId.find(networkId); it != _byNetworkId.end()) {
                return it->second;
            }

            const std::uint16_t slot = TakeSlot();

            if (slot >= WireId::kMaxSlots) {
                return WireId {WireId::kInvalid};
            }

            const WireId id = WireId::Make(slot, _generation[slot]);

            _slotNetworkId[slot] = networkId;
            _slotLive[slot]      = true;
            _byNetworkId[networkId] = id;

            return id;
        }

        // Frees the slot and invalidates every id minted for it. Call on
        // despawn: NOT calling it leaks a slot, and calling it too early
        // makes live motion undeliverable.
        void Release(std::uint64_t networkId) {
            const auto it = _byNetworkId.find(networkId);

            if (it == _byNetworkId.end()) {
                return;
            }

            const std::uint16_t slot = it->second.Slot();

            if (slot < WireId::kMaxSlots) {
                // Wraps within kGenerationBits. Wrapping is not a
                // correctness hole on its own: a stale packet would have to
                // survive 16 reuses of the same slot AND the free-list lap
                // to collide, which is far longer than any datagram lives.
                _generation[slot]    = static_cast<std::uint8_t>((_generation[slot] + 1) & WireId::kGenerationMask);
                _slotLive[slot]      = false;
                _slotNetworkId[slot] = 0;
                _freeSlots.push_back(slot);
            }

            _byNetworkId.erase(it);
        }

        // Resolves a wire id back to its network id. Returns 0 when the id
        // names a dead slot or a stale generation - which is exactly the
        // case a caller must treat as "drop this packet", never as "vehicle
        // zero".
        [[nodiscard]] std::uint64_t Resolve(WireId id) const {
            if (!id.Valid()) {
                return 0;
            }

            const std::uint16_t slot = id.Slot();

            if (slot >= WireId::kMaxSlots || slot >= _slotLive.size() || !_slotLive[slot]) {
                return 0;
            }

            if (_generation[slot] != id.Generation()) {
                return 0;
            }

            return _slotNetworkId[slot];
        }

        [[nodiscard]] WireId Find(std::uint64_t networkId) const {
            const auto it = _byNetworkId.find(networkId);

            return it == _byNetworkId.end() ? WireId {WireId::kInvalid} : it->second;
        }

        // Adopts a mapping minted by the server. Clients use this when a
        // spawn/resync/sync-owner packet tells them a vehicle's wire id.
        void Adopt(std::uint64_t networkId, WireId id) {
            if (!id.Valid()) {
                return;
            }

            const std::uint16_t slot = id.Slot();

            if (slot >= WireId::kMaxSlots || slot >= _slotLive.size()) {
                return;
            }

            // A slot being reassigned to a different vehicle drops the old
            // mapping first, or the network id index keeps pointing at a
            // slot that no longer belongs to it.
            if (_slotLive[slot] && _slotNetworkId[slot] != networkId) {
                _byNetworkId.erase(_slotNetworkId[slot]);
            }

            _generation[slot]    = id.Generation();
            _slotNetworkId[slot] = networkId;
            _slotLive[slot]      = true;
            _byNetworkId[networkId] = id;
        }

        void Clear() {
            _byNetworkId.clear();
            _freeSlots.clear();
            _slotLive.assign(WireId::kMaxSlots, false);
            _slotNetworkId.assign(WireId::kMaxSlots, 0);
            _generation.assign(WireId::kMaxSlots, 0);
            _nextSlot = 0;
        }

        [[nodiscard]] std::size_t LiveCount() const {
            return _byNetworkId.size();
        }

      private:
        [[nodiscard]] std::uint16_t TakeSlot() {
            // Oldest-first: prefer a never-used slot, then the free list in
            // release order.
            if (_nextSlot < WireId::kMaxSlots) {
                return _nextSlot++;
            }

            if (!_freeSlots.empty()) {
                const std::uint16_t slot = _freeSlots.front();

                _freeSlots.erase(_freeSlots.begin());
                return slot;
            }

            return WireId::kMaxSlots;
        }

        std::unordered_map<std::uint64_t, WireId> _byNetworkId;
        std::vector<std::uint64_t> _slotNetworkId;
        std::vector<std::uint8_t>  _generation;
        std::vector<bool>          _slotLive;
        std::vector<std::uint16_t> _freeSlots;
        std::uint16_t              _nextSlot = 0;
    };
} // namespace VantaMP::Shared::RPC::Common
