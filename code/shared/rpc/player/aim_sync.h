#pragma once

#include "common.h"
#include "../common/sequence.h"

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // Where a player's gun is pointing.
    //
    // SEPARATE FROM MOVEMENT BECAUSE THE CADENCE IS DIFFERENT, which is the
    // only reason worth splitting a packet. Aim changes constantly while a
    // player is aiming and barely at all while they simply run, so carrying it
    // inside every movement packet paid for it on every tick of a firefight
    // AND every tick of a cross-town jog. Split, a running player sends
    // nothing here at all.
    //
    // It also has a different consumer: these two angles drive the upper-body
    // IK (CPedIK::PointGunInDirection), not the movement task.
    //
    // NOT THE SAME AS cameraRotation, which stays in PlayerOnFootSync. The
    // camera is what the STICK is resolved against; these are what the HANDS
    // follow. Reading the IK straight off the camera makes a remote player's
    // arms step around in tenths, because the camera is snapped onto the wire
    // at 10 Hz - MTA sends both arm directions for exactly this reason.
    //
    // UNRELIABLE, LATEST-WINS. A missed aim packet is corrected 100 ms later
    // and interpolated in between; a retransmitted stale one would drag the
    // arms backwards.
    struct PlayerAimSync {
        static constexpr const char *kIdentifier = "VMP:PA";

        Common::WireId playerId {};

        std::uint16_t sequence = 0;

        // Both are always present: this packet exists only to carry them, so a
        // change mask would be two bits to decide whether to send four - and
        // an aim update with neither angle would be a packet with no content.
        float aimHorizontal = 0.0f;
        float aimVertical   = 0.0f;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, playerId.value);
            bs->Serialize(write, sequence);

            // 16 bits each over -pi..pi. These were float32 - four bytes to
            // express an angle to a precision no ped arm can display.
            Player::SerializeAngle(bs, write, aimHorizontal);
            Player::SerializeAngle(bs, write, aimVertical);
        }
    };
} // namespace VantaMP::Shared::RPC
