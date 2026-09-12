#pragma once

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

    // POSITION: GTA:SA's world is 6000x6000 and the playable Z band is well
    // inside -1000..2000. 26 bits over +/-8192 gives a 0.00024 m step - far
    // below the millimetre: MEASURED worst-case error across the playable
    // map is 0.12 mm, against a 0.244 mm step. The bound is deliberately
    // wider than the 6000x6000 map so a script teleport past the edge
    // clamps at a sane place instead of wrapping.
    inline constexpr float kPositionLimit = 8192.0f;
    inline constexpr int   kPositionBits  = 26;

    // ROTATION: a unit quaternion, 3 components at 16 bits over -1..1 plus 2
    // bits naming the dropped largest component ("smallest three"). MEASURED
    // worst-case angular error over 200k random orientations is 0.0035 deg -
    // far below what a bike's suspension moves in a single frame, which is
    // the error that actually matters here. Full float32 quat is 128 bits;
    // this is 50.
    inline constexpr int kQuatComponentBits = 16;

    // VELOCITY: MTA's scheme (SVelocitySync). A stationary car - the common
    // case for an unoccupied vehicle - costs ONE BIT. Otherwise magnitude as
    // float32 plus a 48-bit normalized direction. Kept rather than replaced
    // because the zero case is what actually dominates, and the direction
    // error at 16 bits per axis is invisible next to GTA's own integrator.
    inline constexpr float kVelocityEpsilon = 1e-4f;

    // HEALTH: GTA vehicle health runs 0..1000 normally and scripts may raise
    // it. 12 bits over 0..2047.5 is MTA's SVehicleHealthSync exactly: a 0.5
    // step, which is finer than any damage event GTA produces.
    inline constexpr float kHealthMax  = 2047.5f;
    inline constexpr int   kHealthBits = 12;

    // Writes a float into `bits` bits over [min,max]. Clamps rather than
    // wrapping: an out-of-range value must land at the edge, never on the
    // far side of the map.
    inline void SerializeFixed(MafiaNet::BitStream *bs, bool write, float &value, float min, float max, int bits) {
        const std::uint32_t maxQ = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);

        if (write) {
            float alpha = (value - min) / (max - min);

            alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);

            auto quantized = static_cast<std::uint32_t>(alpha * static_cast<float>(maxQ) + 0.5f);

            bs->WriteBits(reinterpret_cast<const unsigned char *>(&quantized), bits);
        }
        else {
            std::uint32_t quantized = 0;

            bs->ReadBits(reinterpret_cast<unsigned char *>(&quantized), bits);
            value = min + (static_cast<float>(quantized) / static_cast<float>(maxQ)) * (max - min);
        }
    }

    inline void SerializePosition(MafiaNet::BitStream *bs, bool write, glm::vec3 &position) {
        SerializeFixed(bs, write, position.x, -kPositionLimit, kPositionLimit, kPositionBits);
        SerializeFixed(bs, write, position.y, -kPositionLimit, kPositionLimit, kPositionBits);
        SerializeFixed(bs, write, position.z, -kPositionLimit, kPositionLimit, kPositionBits);
    }

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

                SerializeFixed(bs, true, value, -kInvSqrt2, kInvSqrt2, kQuatComponentBits);
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

                SerializeFixed(bs, false, value, -kInvSqrt2, kInvSqrt2, kQuatComponentBits);
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
    inline void SerializeVelocity(MafiaNet::BitStream *bs, bool write, glm::vec3 &velocity) {
        if (write) {
            const float magnitude = glm::length(velocity);

            if (!std::isfinite(magnitude) || magnitude < kVelocityEpsilon) {
                bs->Write(false);
                return;
            }

            bs->Write(true);
            bs->Write(magnitude);

            const glm::vec3 direction = velocity / magnitude;

            bs->WriteNormVector(direction.x, direction.y, direction.z);
        }
        else {
            bool moving = false;

            bs->Read(moving);

            if (!moving) {
                velocity = glm::vec3(0.0f);
                return;
            }

            float magnitude = 0.0f;
            float x = 0.0f, y = 0.0f, z = 0.0f;

            bs->Read(magnitude);
            bs->ReadNormVector(x, y, z);

            velocity = glm::vec3(x, y, z) * magnitude;
        }
    }

    inline void SerializeHealth(MafiaNet::BitStream *bs, bool write, float &health) {
        SerializeFixed(bs, write, health, 0.0f, kHealthMax, kHealthBits);
    }
} // namespace VantaMP::Shared::RPC::Vehicle
