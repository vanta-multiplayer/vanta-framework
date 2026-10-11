#pragma once

#include "common.h"
#include "../common/sequence.h"

#include <mafianet/BitStream.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // Full authoritative correction for one vehicle. MTA's VEHICLE_RESYNC.
    //
    // THIS IS NOT THE MOTION PACKET AND MUST NOT BE SENT AT ITS RATE. It
    // carries no change mask - every field is present, unconditionally -
    // which is exactly why it is the recovery path and not the steady-state
    // one. Sending this every tick would cost more than the protocol it
    // replaced.
    //
    // RELIABLE, because a correction that is dropped leaves the receiver
    // wrong indefinitely: there is no "next one in 100 ms" behind it.
    //
    // Sent for:
    //   - a detected divergence the normal stream cannot close
    //   - an ownership change where the new owner needs a known-good base
    //   - an explicit resync request
    //   - recovery after the receiver has been out of range and returned
    //
    // Stream-in is NOT in that list: a newly constructed replica already
    // carries full state through ReplicaManager3's construction bitstream,
    // and duplicating it here would send the same fact twice.
    struct VehicleResync {
        static constexpr const char *kIdentifier = "VantaMP::VehicleResync";

        std::uint64_t vehicleId = 0;

        // Adopted by the receiver as its newest-applied sequence, so an
        // in-flight driver sync sent BEFORE this correction cannot land
        // after it and undo it. Without this the corrected car snaps back to
        // the diverged pose for one tick.
        Sequence sequence = 0;

        glm::vec3 position {};
        glm::quat rotation = glm::identity<glm::quat>();
        glm::vec3 velocity {};
        glm::vec3 turnSpeed {};

        float health = 1000.0f;

        std::uint8_t flags = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, vehicleId);
            bs->Serialize(write, sequence);

            Common::SerializePosition(bs, write, position);
            Vehicle::SerializeRotation(bs, write, rotation);
            Common::SerializeVelocity(bs, write, velocity);
            Common::SerializeVelocity(bs, write, turnSpeed);
            Vehicle::SerializeHealth(bs, write, health);

            if (write) {
                bs->WriteBits(reinterpret_cast<const unsigned char *>(&flags), Vehicle::VehicleFlag::kBits);
            }
            else {
                flags = 0;
                bs->ReadBits(reinterpret_cast<unsigned char *>(&flags), Vehicle::VehicleFlag::kBits);
            }
        }
    };
} // namespace VantaMP::Shared::RPC
