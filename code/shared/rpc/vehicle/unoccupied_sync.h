#pragma once

#include "common.h"
#include "../common/wire_id.h"

#include <mafianet/BitStream.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace VantaMP::Shared::RPC {
    // One unoccupied vehicle's delta. MTA's SUnoccupiedVehicleSync.
    //
    // Only the fields that actually changed are serialized, named by a
    // 5-bit mask. A parked car that nothing touched produces NO entry at
    // all - it is never added to a batch - which is the point: a car park
    // must cost nothing per tick.
    struct VehicleUnoccupiedEntry {
        // Compact id: this is repeated once PER VEHICLE inside a batch, so
        // it is the field that decides how many vehicles fit in one
        // datagram. 64 bits here cost 6 bytes a vehicle for nothing.
        Common::WireId vehicleId {};
        std::uint8_t    fields = 0;

        glm::vec3 position {};
        glm::quat rotation = glm::identity<glm::quat>();
        glm::vec3 velocity {};
        glm::vec3 turnSpeed {};
        float     health = 1000.0f;

        [[nodiscard]] bool Has(std::uint8_t field) const {
            return (fields & field) != 0;
        }

        [[nodiscard]] bool Empty() const {
            return fields == 0;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, vehicleId.value);

            if (write) {
                bs->WriteBits(reinterpret_cast<const unsigned char *>(&fields), Vehicle::UnoccupiedField::kCount);
            }
            else {
                fields = 0;
                bs->ReadBits(reinterpret_cast<unsigned char *>(&fields), Vehicle::UnoccupiedField::kCount);
            }

            if (Has(Vehicle::UnoccupiedField::kPosition)) {
                Vehicle::SerializePosition(bs, write, position);
            }

            if (Has(Vehicle::UnoccupiedField::kRotation)) {
                Vehicle::SerializeRotation(bs, write, rotation);
            }

            if (Has(Vehicle::UnoccupiedField::kVelocity)) {
                Vehicle::SerializeVelocity(bs, write, velocity);
            }

            if (Has(Vehicle::UnoccupiedField::kTurnSpeed)) {
                Vehicle::SerializeVelocity(bs, write, turnSpeed);
            }

            if (Has(Vehicle::UnoccupiedField::kHealth)) {
                Vehicle::SerializeHealth(bs, write, health);
            }
        }

        // Upper bound on this entry's wire size, in BITS. Used to stop a
        // batch before it crosses the MTU; deliberately an over-estimate, so
        // the batch stops early rather than fragmenting.
        [[nodiscard]] static constexpr int MaxBits() {
            return Common::WireId::kBits                     // vehicleId (compact)
                 + Vehicle::UnoccupiedField::kCount           // mask
                 + 3 * Vehicle::kPositionBits                 // position
                 + 2 + 3 * Vehicle::kQuatComponentBits        // rotation
                 + 2 * (1 + 32 + 48)                          // velocity + turn speed, worst case
                 + Vehicle::kHealthBits;                      // health
        }
    };

    // A batch of unoccupied vehicle deltas in one datagram.
    //
    // WHY BATCH AT ALL: each RPC pays ~9 bytes of RPC4 header (two message
    // ids plus the Huffman-encoded identifier, then a byte align). Twenty
    // separate packets pay that twenty times; one batch pays it once. That
    // header is the entire reason this packet exists.
    //
    // MTU IS RESPECTED BY CONSTRUCTION. MafiaNet's MAXIMUM_MTU_SIZE is 1400;
    // anything past it fragments at the IP layer, where losing one fragment
    // loses the whole datagram. The writer stops adding entries at
    // kMaxPayloadBytes and the caller sends another batch.
    struct VehicleUnoccupiedSync {
        static constexpr const char *kIdentifier = "VMP:VU";

        // 1400 MTU, less RakNet's own datagram/reliability headers and the
        // RPC4 identifier. 1100 leaves generous room for all of it: the cost
        // of being wrong here is silent fragmentation, so this is not a
        // number to tune aggressively.
        static constexpr std::size_t kMaxPayloadBytes = 1100;

        // Guards a hostile or corrupted count field from allocating
        // unbounded memory before a single entry has been validated.
        static constexpr std::uint16_t kMaxEntries = 256;

        std::uint16_t sequence = 0;

        std::vector<VehicleUnoccupiedEntry> entries;

        // How many more entries fit before the MTU cap, given what is
        // already queued. The sender uses this to close a batch and start
        // another rather than letting one datagram fragment.
        [[nodiscard]] static bool Fits(std::size_t currentBits) {
            return (currentBits + VehicleUnoccupiedEntry::MaxBits()) / 8 <= kMaxPayloadBytes;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, sequence);

            if (write) {
                auto count = static_cast<std::uint16_t>(entries.size());

                bs->Write(count);

                for (auto &entry : entries) {
                    entry.Serialize(bs, true);
                }
            }
            else {
                std::uint16_t count = 0;

                bs->Read(count);

                if (count > kMaxEntries) {
                    count = kMaxEntries;
                }

                entries.clear();
                entries.reserve(count);

                for (std::uint16_t i = 0; i < count; ++i) {
                    VehicleUnoccupiedEntry entry;

                    entry.Serialize(bs, false);
                    entries.push_back(entry);
                }
            }
        }
    };
} // namespace VantaMP::Shared::RPC
