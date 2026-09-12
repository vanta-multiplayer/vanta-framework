#pragma once

#include "common.h"
#include "../common/sequence.h"
#include "../common/wire_id.h"

#include <mafianet/BitStream.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // The vehicle a driver is actively controlling. MTA's
    // PLAYER_VEHICLE_PURESYNC.
    //
    // UNRELIABLE, LATEST-WINS. There is no value in retransmitting snapshot N
    // once N+1 has arrived - a late one would drag the car backwards - so
    // this goes out Unreliable and the receiver drops anything older than the
    // newest pose it has applied (see `sequence`). A dropped packet is
    // covered by the next one 100 ms later plus receiver-side interpolation.
    //
    // ONLY CONTINUOUS DRIVING STATE LIVES HERE. Colours, upgrades, plate,
    // locks, paintjob and every other slowly-changing field stay on the
    // replica's delta channel, where they cost one bit each while unchanged.
    // Damage is its own packet: it is an event, not a per-tick fact.
    struct VehicleDriverSync {
        // Short on purpose - RPC4 Huffman-encodes this string into EVERY
        // packet. See the note in common.h: a descriptive name would cost
        // more than the entire payload.
        static constexpr const char *kIdentifier = "VMP:VD";

        // COMPACT, NOT CANONICAL. 16 bits (12 slot + 4 generation) instead
        // of the 64-bit NetworkID, which at this packet's rate was the
        // largest single field in it. See wire_id.h for why the generation
        // half is not optional.
        Common::WireId vehicleId {};

        // Wraps at 65536 ticks (~109 minutes at 100 ms). Compared with a
        // signed-difference test so wrap is harmless, and it is what makes a
        // reordered UDP datagram detectable.
        std::uint16_t sequence = 0;

        std::uint16_t fields = 0;

        glm::vec3 position {};
        glm::quat rotation = glm::identity<glm::quat>();
        glm::vec3 velocity {};
        glm::vec3 turnSpeed {};

        float health = 1000.0f;

        std::int8_t  steer       = 0;
        std::uint8_t accelerator = 0;
        std::uint8_t brake       = 0;
        std::int8_t  gasPedal    = 0;
        std::int8_t  brakePedal  = 0;

        std::uint8_t flags = 0;

        [[nodiscard]] bool Has(std::uint16_t field) const {
            return (fields & field) != 0;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, vehicleId.value);
            bs->Serialize(write, sequence);

            // The mask is the contract: read it first, then take only what it
            // names. A field the mask does not name is NOT MODIFIED by the
            // receiver - it is not implicitly zero.
            if (write) {
                bs->WriteBits(reinterpret_cast<const unsigned char *>(&fields), Vehicle::DriverField::kCount);
            }
            else {
                fields = 0;
                bs->ReadBits(reinterpret_cast<unsigned char *>(&fields), Vehicle::DriverField::kCount);
            }

            if (Has(Vehicle::DriverField::kPosition)) {
                Vehicle::SerializePosition(bs, write, position);
            }

            if (Has(Vehicle::DriverField::kRotation)) {
                Vehicle::SerializeRotation(bs, write, rotation);
            }

            if (Has(Vehicle::DriverField::kVelocity)) {
                Vehicle::SerializeVelocity(bs, write, velocity);
            }

            if (Has(Vehicle::DriverField::kTurnSpeed)) {
                Vehicle::SerializeVelocity(bs, write, turnSpeed);
            }

            if (Has(Vehicle::DriverField::kHealth)) {
                Vehicle::SerializeHealth(bs, write, health);
            }

            if (Has(Vehicle::DriverField::kControls)) {
                // ALREADY BYTE-SIZED IN GTA. m_fSteerAngle and the pedals are
                // read into int8/uint8 at the SDK edge, so there is nothing
                // to gain by quantizing further - and a narrower steer would
                // be visible as a stepped wheel.
                bs->Serialize(write, steer);
                bs->Serialize(write, accelerator);
                bs->Serialize(write, brake);
                bs->Serialize(write, gasPedal);
                bs->Serialize(write, brakePedal);
            }

            if (Has(Vehicle::DriverField::kFlags)) {
                if (write) {
                    bs->WriteBits(reinterpret_cast<const unsigned char *>(&flags), Vehicle::VehicleFlag::kBits);
                }
                else {
                    flags = 0;
                    bs->ReadBits(reinterpret_cast<unsigned char *>(&flags), Vehicle::VehicleFlag::kBits);
                }
            }
        }
    };
} // namespace VantaMP::Shared::RPC
