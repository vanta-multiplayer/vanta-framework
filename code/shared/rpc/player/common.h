#pragma once

#include "shared/rpc/common/wire_id.h"

#include <mafianet/BitStream.h>

#include <glm/vec3.hpp>

#include <cmath>
#include <cstdint>

namespace VantaMP::Shared::RPC::Player {
    // Shared wire primitives for the player protocol.
    //
    // Deliberately NOT folded into the vehicle quantizers. A player and a
    // vehicle both contain a Vec3, which is not a reason to share a
    // serializer: the vehicle carries a full 3D orientation because a car can
    // sit on its roof, while a ped has a single heading and nothing else. The
    // genuinely identical pieces - the compact entity id, fixed-point
    // helpers - are shared through rpc/common/. The rest is not.

    // --- Change masks ---------------------------------------------------------
    //
    // PRESENCE IS NOT VALUE, exactly as for vehicles. A stick returning to
    // centre, a released trigger and a cleared button mask are all valid
    // ZERO values, so the reader must branch on the mask bit and never on the
    // decoded number.
    namespace OnFootField {
        inline constexpr std::uint8_t kPosition = 1u << 0;
        inline constexpr std::uint8_t kHeading  = 1u << 1;
        inline constexpr std::uint8_t kVelocity = 1u << 2;
        inline constexpr std::uint8_t kInput    = 1u << 3;
        inline constexpr std::uint8_t kFlags    = 1u << 4;

        // AIM RIDES THE MOVEMENT PACKET, GATED ON ACTUALLY AIMING.
        //
        // This was briefly its own PlayerAimSync packet. That was wrong: a
        // separate RPC pays a second Huffman identifier (~9 B) on every
        // update, which is more than the two quantized angles cost.
        //
        // MTA puts the same two angles inside keysync and puresync, written
        // only when `ControllerState.RightShoulder1 || ButtonCircle` - i.e.
        // only when the player is genuinely aiming or firing
        // (CNetAPI.cpp:1128, CKeysyncPacket.cpp:56). A running player pays
        // nothing for aim, and an aiming one pays 4 bytes rather than a whole
        // second packet. Same idea here, keyed off the Fire/Aim button bits.
        inline constexpr std::uint8_t kAim      = 1u << 5;
        inline constexpr std::uint8_t kCount    = 6;
    } // namespace OnFootField

    // Pose flags. GTA cannot derive these from a stick position - entering
    // and leaving a crouch are animations that take time and can be refused
    // mid-transition, so the owner reports the state rather than the button.
    namespace PedFlag {
        inline constexpr std::uint8_t kDucked  = 1u << 0;
        inline constexpr std::uint8_t kInAir   = 1u << 1;
        inline constexpr std::uint8_t kInWater = 1u << 2;
        inline constexpr int          kBits    = 3;
    } // namespace PedFlag

    // --- Quantization ---------------------------------------------------------

    // POSITION: the same bound and step as vehicles (26 bits over +/-8192,
    // 0.24 mm). A ped needs no more precision than a car and the map is the
    // same map, so reusing the constants keeps one answer to "where can an
    // entity be".
    inline constexpr float kPositionLimit = 8192.0f;
    inline constexpr int   kPositionBits  = 26;

    // ANGLES: heading, camera rotation and the two aim components are all
    // radians over -pi..pi. 16 bits gives a 0.0055 degree step, which is far
    // below what the ped IK or the walk direction can show - and these were
    // float32 before, which is four bytes to express an angle a compass could
    // not distinguish.
    inline constexpr int kAngleBits = 16;

    // VELOCITY: a ped tops out around 12 m/s sprinting; a fall or a jetpack
    // can exceed that, so the bound is generous. 16 bits per axis over
    // +/-64 m/s is a 2 mm/s step, which is well under what the receiver's
    // interpolation can express.
    inline constexpr float kVelocityLimit = 64.0f;
    inline constexpr int   kVelocityBits  = 16;

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

    inline void SerializeVelocity(MafiaNet::BitStream *bs, bool write, glm::vec3 &velocity) {
        SerializeFixed(bs, write, velocity.x, -kVelocityLimit, kVelocityLimit, kVelocityBits);
        SerializeFixed(bs, write, velocity.y, -kVelocityLimit, kVelocityLimit, kVelocityBits);
        SerializeFixed(bs, write, velocity.z, -kVelocityLimit, kVelocityLimit, kVelocityBits);
    }

    // WRAPPED, NOT CLAMPED. An angle that arrives just past pi must come back
    // just past -pi, not pinned at the limit - a clamp there would make a ped
    // turning through north stick and then jump.
    inline void SerializeAngle(MafiaNet::BitStream *bs, bool write, float &radians) {
        constexpr float kPi    = 3.14159265358979f;
        constexpr float kTwoPi = kPi * 2.0f;

        if (write) {
            float wrapped = std::fmod(radians + kPi, kTwoPi);

            if (wrapped < 0.0f) {
                wrapped += kTwoPi;
            }

            wrapped -= kPi;

            if (!std::isfinite(wrapped)) {
                wrapped = 0.0f;
            }

            SerializeFixed(bs, true, wrapped, -kPi, kPi, kAngleBits);
        }
        else {
            SerializeFixed(bs, false, radians, -kPi, kPi, kAngleBits);
        }
    }

    // THE STICKS ARE EIGHT BITS ON THE WIRE, AND THAT IS NOT A GUESS.
    //
    // GTA's CControllerState stores them as short, and VantaMP reads them as
    // int16 - but the value range a pad actually produces is -128..127. MTA
    // transmits them as a char and scales by 127/128 on the way out and
    // 128/127 on the way back (SyncStructures.h, SPlayerPuresyncPacket), which
    // is where this encoding comes from. Sending int16 was two bytes to carry
    // one byte of information.
    inline void SerializeStick(MafiaNet::BitStream *bs, bool write, std::int16_t &value) {
        if (write) {
            auto packed = static_cast<std::int8_t>(std::lround(static_cast<float>(value) * 127.0f / 128.0f));

            bs->Write(packed);
        }
        else {
            std::int8_t packed = 0;

            bs->Read(packed);
            value = static_cast<std::int16_t>(std::lround(static_cast<float>(packed) * 128.0f / 127.0f));
        }
    }
} // namespace VantaMP::Shared::RPC::Player
