#pragma once

#include "shared/rpc/common/quantize.h"
#include "shared/rpc/common/wire_id.h"

#include <mafianet/BitStream.h>

#include <glm/vec3.hpp>

#include <cmath>
#include <cstdint>

namespace VantaMP::Shared::RPC::Player {
    // Shared wire primitives for the player protocol.
    //
    // Position, velocity and angle encoding live in rpc/common/quantize.h and
    // are shared with the vehicle protocol: a metre is a metre whichever kind
    // of entity is standing on it. What stays HERE is what is genuinely
    // player-shaped - the field mask, the pose flags, and the controller
    // stick encoding, none of which a vehicle has.

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

    // --- Player-specific encoding --------------------------------------------

    // THE STICKS ARE EIGHT BITS ON THE WIRE, AND THAT IS NOT A GUESS.
    //
    // GTA's CControllerState stores them as short, and VantaMP reads them as
    // int16 - but the value range a pad actually produces is -128..127. MTA
    // transmits them as a char and scales by 127/128 on the way out and
    // 128/127 on the way back (SyncStructures.h). Sending int16 was two bytes
    // to carry one byte of information.
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
