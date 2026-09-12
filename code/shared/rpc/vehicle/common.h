#pragma once

#include "shared/rpc/common/quantize.h"
#include "shared/vehicle/validate.h"

#include <mafianet/BitStream.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <cstdint>

namespace VantaMP::Shared::RPC::Vehicle {
    // Shared wire primitives for the vehicle protocol.
    //
    // THE IDENTIFIER IS THE PACKET'S BIGGEST FIXED COST, so the hot ones are
    // short. RPC4::Signal writes ID_RPC_PLUGIN, ID_RPC4_SIGNAL, then the
    // identifier STRING Huffman-compressed, then byte-aligns
    // (RPC4Plugin.cpp:Signal). At ~6 bits a character
    // "VantaMP::VehicleDriverSync" costs 23 bytes of header before a single
    // byte of payload - more than the payload itself. "VMP:VD" costs 9.
    //
    // The replica path has no such cost (RM3 addresses entities by an
    // internal id, no string), which is why only genuinely packet-shaped
    // state moved off it.

    // --- Change masks ---------------------------------------------------------
    //
    // PRESENCE IS NOT VALUE. A field absent from the mask means DO NOT
    // MODIFY; a field present carrying zero means SET TO ZERO. Every reader
    // below branches on the mask bit, never on the decoded value, because
    // zero is valid for every one of these fields (a car at rest, a released
    // throttle, a repaired panel).
    namespace DriverField {
        inline constexpr std::uint16_t kPosition = 1u << 0;
        inline constexpr std::uint16_t kRotation = 1u << 1;
        inline constexpr std::uint16_t kVelocity = 1u << 2;
        inline constexpr std::uint16_t kTurnSpeed = 1u << 3;
        inline constexpr std::uint16_t kControls = 1u << 4;
        inline constexpr std::uint16_t kHealth   = 1u << 5;
        inline constexpr std::uint16_t kFlags    = 1u << 6;
        inline constexpr std::uint16_t kCount    = 7;
    } // namespace DriverField

    namespace UnoccupiedField {
        inline constexpr std::uint8_t kPosition  = 1u << 0;
        inline constexpr std::uint8_t kRotation  = 1u << 1;
        inline constexpr std::uint8_t kVelocity  = 1u << 2;
        inline constexpr std::uint8_t kTurnSpeed = 1u << 3;
        inline constexpr std::uint8_t kHealth    = 1u << 4;
        inline constexpr std::uint8_t kCount     = 5;
    } // namespace UnoccupiedField

    namespace DamageField {
        inline constexpr std::uint8_t kEngine     = 1u << 0;
        inline constexpr std::uint8_t kDoors      = 1u << 1;
        inline constexpr std::uint8_t kWheels     = 1u << 2;
        inline constexpr std::uint8_t kPanels     = 1u << 3;
        inline constexpr std::uint8_t kLights     = 1u << 4;
        inline constexpr std::uint8_t kDoorRatios = 1u << 5;
        inline constexpr std::uint8_t kHealth     = 1u << 6;
        inline constexpr std::uint8_t kCount      = 7;
    } // namespace DamageField

    // Physical flags. Packed into 6 bits rather than six byte-sized bools.
    namespace VehicleFlag {
        inline constexpr std::uint8_t kOnGround  = 1u << 0;
        inline constexpr std::uint8_t kEngineOn  = 1u << 1;
        inline constexpr std::uint8_t kSirenOn   = 1u << 2;
        inline constexpr std::uint8_t kHandbrake = 1u << 3;
        inline constexpr std::uint8_t kLightsOn  = 1u << 4;
        inline constexpr std::uint8_t kInWater   = 1u << 5;
        inline constexpr int          kBits      = 6;
    } // namespace VehicleFlag

    // --- Quantization ---------------------------------------------------------
    //
    // Every choice below is justified by measured error against the field's
    // real range, not by how small it looks. See docs/vehicle-protocol.md.

    // Position, velocity and the fixed-point helper live in
    // rpc/common/quantize.h, shared with the player protocol. What stays here
    // is what is genuinely vehicle-shaped: a full 3D orientation (a car can
    // sit on its roof, a ped cannot) and the health range.

    // ROTATION: a unit quaternion, 3 components at 16 bits over -1..1 plus 2
    // bits naming the dropped largest component ("smallest three"). MEASURED
    // worst-case angular error over 200k random orientations is 0.0035 deg -
    // far below what a bike's suspension moves in a single frame, which is
    // the error that actually matters here. Full float32 quat is 128 bits;
    // this is 50.
    inline constexpr int kQuatComponentBits = 16;

    // HEALTH: GTA vehicle health runs 0..1000 normally and scripts may raise
    // it. 12 bits over 0..2047.5 is MTA's SVehicleHealthSync exactly: a 0.5
    // step, which is finer than any damage event GTA produces.
    inline constexpr float kHealthMax  = 2047.5f;
    inline constexpr int   kHealthBits = 12;

    // Writes a float into `bits` bits over [min,max]. Clamps rather than
    // wrapping: an out-of-range value must land at the edge, never on the
    // far side of the map.


    // Smallest-three quaternion. The largest component is dropped and
    // recovered from the unit-length constraint; its sign is normalised away
    // by negating the whole quaternion when needed (q and -q are the same
    // rotation, so this is lossless in meaning).
    inline void SerializeRotation(MafiaNet::BitStream *bs, bool write, glm::quat &rotation) {
        constexpr float kInvSqrt2 = 0.70710678f;

        if (write) {
            glm::quat q = glm::normalize(rotation);

            // Guard a degenerate quat rather than writing NaN: a corrupted
            // source must not be able to poison every receiver's matrix.
            if (!Shared::IsValidRotation(q)) {
                q = glm::identity<glm::quat>();
            }

            const float components[4] = {q.x, q.y, q.z, q.w};

            std::uint32_t largest = 0;

            for (std::uint32_t i = 1; i < 4; ++i) {
                if (std::fabs(components[i]) > std::fabs(components[largest])) {
                    largest = i;
                }
            }

            // Negate so the dropped component is positive; its sign then
            // needs no bit.
            const float sign = components[largest] < 0.0f ? -1.0f : 1.0f;

            bs->WriteBits(reinterpret_cast<const unsigned char *>(&largest), 2);

            for (std::uint32_t i = 0; i < 4; ++i) {
                if (i == largest) {
                    continue;
                }

                float value = components[i] * sign;

                Common::SerializeFixed(bs, true, value, -kInvSqrt2, kInvSqrt2, kQuatComponentBits);
            }
        }
        else {
            std::uint32_t largest = 0;

            bs->ReadBits(reinterpret_cast<unsigned char *>(&largest), 2);

            float components[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float sumOfSquares  = 0.0f;

            for (std::uint32_t i = 0; i < 4; ++i) {
                if (i == largest) {
                    continue;
                }

                float value = 0.0f;

                Common::SerializeFixed(bs, false, value, -kInvSqrt2, kInvSqrt2, kQuatComponentBits);
                components[i] = value;
                sumOfSquares += value * value;
            }

            const float remainder  = 1.0f - sumOfSquares;
            components[largest]    = remainder > 0.0f ? std::sqrt(remainder) : 0.0f;

            rotation = glm::normalize(glm::quat(components[3], components[0], components[1], components[2]));
        }
    }

    // MTA's SVelocitySync: one bit for the zero case, which is what an
    // unoccupied vehicle almost always is.

    inline void SerializeHealth(MafiaNet::BitStream *bs, bool write, float &health) {
        Common::SerializeFixed(bs, write, health, 0.0f, kHealthMax, kHealthBits);
    }
} // namespace VantaMP::Shared::RPC::Vehicle
