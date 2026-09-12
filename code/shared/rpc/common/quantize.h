#pragma once

#include <mafianet/BitStream.h>

#include <glm/vec3.hpp>

#include <cmath>
#include <cstdint>

namespace VantaMP::Shared::RPC::Common {
    // Fixed-point encoders shared by the player and vehicle protocols.
    //
    // GENUINELY IDENTICAL SEMANTICS, which is the only reason these are
    // shared. A position is a position and a metre-per-second is a
    // metre-per-second whether a ped or a car is doing it - unlike
    // orientation, where a vehicle needs a full quaternion and a ped needs one
    // heading, so those stay in their own protocols.
    //
    // Every bound below is sized to GTA:SA, not to float32. That is where the
    // savings are: a float32 spans 1e38 to carry a number that never leaves
    // +/-100.

    // --- World bounds ---------------------------------------------------------
    //
    // GTA:SA's playable map is 6000x6000 (-3000..3000 on X and Y). The bound
    // here is +/-4096 - wider than the map so a script teleport past the edge
    // clamps somewhere sane, but nowhere near the +/-8192 this used to carry,
    // which spent a bit per axis on space the game cannot reach.
    //
    // MTA ships 10 fractional bits (~1 mm) over a similar range and nobody
    // reports jitter; 23 bits here gives 0.98 mm, which matches.
    inline constexpr float kWorldLimitXY = 4096.0f;
    inline constexpr int   kPositionBitsXY = 23;

    // Z IS NOT X. Height runs about -100 (sea floor) to 1500 (Mt Chiliad plus
    // an aircraft ceiling), so spending the same 23 bits on it as on a
    // horizontal axis wastes three of them on altitudes nothing can occupy.
    // 20 bits over -256..1792 gives 1.95 mm - the same order as X/Y, which
    // matters because a bike's suspension travel is vertical and a coarse Z
    // would show as chassis bounce.
    inline constexpr float kWorldMinZ = -256.0f;
    inline constexpr float kWorldMaxZ = 1792.0f;
    inline constexpr int   kPositionBitsZ = 20;

    // --- Velocity -------------------------------------------------------------
    //
    // THE OLD SCHEME SPENT A FLOAT32 ON THE MAGNITUDE. MTA's SVelocitySync
    // writes a zero-bit, then a float32 magnitude and a 48-bit normalized
    // direction: 81 bits. The float32 is the waste - GTA's fastest vehicle
    // does about 65 m/s, terminal velocity is near 50, and a Hydra in a dive
    // tops out around 90. Nothing needs 1e38.
    //
    // Quantizing each axis directly over +/-128 m/s costs 14 bits an axis:
    // 43 bits total against 81, and the error is 15.6 mm/s. A receiver
    // integrating that for a full 100 ms tick drifts 1.6 mm, which is below
    // the POSITION quantizer's own step - so it cannot be the visible error.
    //
    // The zero case still costs ONE BIT, because a stationary entity is the
    // common case for anything unoccupied and that property is worth keeping.
    inline constexpr float kVelocityLimit = 128.0f;
    inline constexpr int   kVelocityBits  = 14;
    inline constexpr float kVelocityEpsilon = 1e-4f;

    // --- Angles ---------------------------------------------------------------
    //
    // 16 bits over -pi..pi is a 0.0055 degree step - below what a ped's IK or
    // a vehicle's heading can display.
    inline constexpr int kAngleBits = 16;

    // Writes a float into `bits` bits over [min,max].
    //
    // CLAMPS RATHER THAN WRAPPING. An out-of-range value must land at the edge
    // of the world, never on the far side of it.
    inline void SerializeFixed(MafiaNet::BitStream *bs, bool write, float &value, float min, float max, int bits) {
        const std::uint32_t maxQ = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);

        if (write) {
            float alpha = (value - min) / (max - min);

            // A NaN fails both comparisons and would otherwise convert to an
            // arbitrary integer, so it is pinned explicitly.
            if (!(alpha >= 0.0f)) {
                alpha = 0.0f;
            }
            else if (alpha > 1.0f) {
                alpha = 1.0f;
            }

            auto quantized = static_cast<std::uint32_t>(alpha * static_cast<float>(maxQ) + 0.5f);

            bs->WriteBits(reinterpret_cast<const unsigned char *>(&quantized), bits);
        }
        else {
            std::uint32_t quantized = 0;

            bs->ReadBits(reinterpret_cast<unsigned char *>(&quantized), bits);
            value = min + (static_cast<float>(quantized) / static_cast<float>(maxQ)) * (max - min);
        }
    }

    // 23 + 23 + 20 = 66 bits, against the 78 a uniform 26-bit encoding cost.
    inline void SerializePosition(MafiaNet::BitStream *bs, bool write, glm::vec3 &position) {
        SerializeFixed(bs, write, position.x, -kWorldLimitXY, kWorldLimitXY, kPositionBitsXY);
        SerializeFixed(bs, write, position.y, -kWorldLimitXY, kWorldLimitXY, kPositionBitsXY);
        SerializeFixed(bs, write, position.z, kWorldMinZ, kWorldMaxZ, kPositionBitsZ);
    }

    // One bit when still, 43 when moving.
    inline void SerializeVelocity(MafiaNet::BitStream *bs, bool write, glm::vec3 &velocity) {
        if (write) {
            const float magnitudeSq = velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z;

            // A non-finite velocity is treated as zero rather than written: it
            // would quantize to an edge value and shove the entity.
            if (!std::isfinite(magnitudeSq) || magnitudeSq < kVelocityEpsilon * kVelocityEpsilon) {
                bs->Write(false);
                return;
            }

            bs->Write(true);
            SerializeFixed(bs, true, velocity.x, -kVelocityLimit, kVelocityLimit, kVelocityBits);
            SerializeFixed(bs, true, velocity.y, -kVelocityLimit, kVelocityLimit, kVelocityBits);
            SerializeFixed(bs, true, velocity.z, -kVelocityLimit, kVelocityLimit, kVelocityBits);
        }
        else {
            bool moving = false;

            bs->Read(moving);

            if (!moving) {
                velocity = glm::vec3(0.0f);
                return;
            }

            SerializeFixed(bs, false, velocity.x, -kVelocityLimit, kVelocityLimit, kVelocityBits);
            SerializeFixed(bs, false, velocity.y, -kVelocityLimit, kVelocityLimit, kVelocityBits);
            SerializeFixed(bs, false, velocity.z, -kVelocityLimit, kVelocityLimit, kVelocityBits);
        }
    }

    // WRAPPED, NOT CLAMPED. An angle just past pi must come back just past
    // -pi: a clamp there would make an entity turning through north stick and
    // then jump.
    inline void SerializeAngle(MafiaNet::BitStream *bs, bool write, float &radians) {
        constexpr float kPi    = 3.14159265358979f;
        constexpr float kTwoPi = kPi * 2.0f;

        if (write) {
            float wrapped = std::isfinite(radians) ? std::fmod(radians + kPi, kTwoPi) : 0.0f;

            if (wrapped < 0.0f) {
                wrapped += kTwoPi;
            }

            wrapped -= kPi;

            SerializeFixed(bs, true, wrapped, -kPi, kPi, kAngleBits);
        }
        else {
            SerializeFixed(bs, false, radians, -kPi, kPi, kAngleBits);
        }
    }
} // namespace VantaMP::Shared::RPC::Common
