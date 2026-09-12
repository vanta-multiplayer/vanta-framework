#pragma once

#include "common.h"
#include "../common/sequence.h"

#include <mafianet/BitStream.h>

#include <glm/vec3.hpp>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // A player moving on foot. MTA's PLAYER_PURESYNC.
    //
    // UNRELIABLE, LATEST-WINS, for the same reason as the vehicle driver
    // stream: retransmitting snapshot N after N+1 has landed would drag the
    // ped backwards. Ordering is per player via `sequence`, not by a RakNet
    // sequenced channel - a sequenced channel is per channel, so one player's
    // packet would discard another's.
    //
    // INPUT, NOT ANIMATION. The stick, pedals and buttons are what GTA's own
    // movement code consumes, so a remote player walks because the game walked
    // them. Replicating which animation is playing would mean every client
    // agreeing about state it did not compute.
    //
    // NOT SENT WHILE SEATED. A seated ped's transform is the vehicle's and
    // VehicleDriverSync already carries it; the player replica keeps carrying
    // the state that is genuinely the player's (weapon, health, aim).
    struct PlayerOnFootSync {
        // Short on purpose: RPC4 Huffman-encodes this string into every
        // packet. See the note in rpc/vehicle/common.h - a descriptive name
        // would cost more than the payload.
        static constexpr const char *kIdentifier = "VMP:PF";

        Common::WireId playerId {};

        std::uint16_t sequence = 0;
        std::uint8_t  fields   = 0;

        glm::vec3 position {};
        glm::vec3 velocity {};

        // The ped's facing. A ped has ONE heading - unlike a vehicle, which
        // needs a full orientation because it can sit on its roof - so this is
        // a single angle, not a quaternion.
        float heading = 0.0f;

        // WHICH WAY THE PLAYER IS LOOKING, which is not which way they face.
        // GTA resolves stick input against the camera, so this travels WITH
        // the input it has to be interpreted against: a tick of skew between
        // them and the ped walks sideways.
        float cameraRotation = 0.0f;

        std::int16_t  leftStickX  = 0;
        std::int16_t  leftStickY  = 0;
        std::uint8_t  accelerator = 0;
        std::uint8_t  brake       = 0;
        std::uint16_t buttons     = 0;

        std::uint8_t flags = 0;

        [[nodiscard]] bool Has(std::uint8_t field) const {
            return (fields & field) != 0;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, playerId.value);
            bs->Serialize(write, sequence);

            if (write) {
                bs->WriteBits(reinterpret_cast<const unsigned char *>(&fields), Player::OnFootField::kCount);
            }
            else {
                fields = 0;
                bs->ReadBits(reinterpret_cast<unsigned char *>(&fields), Player::OnFootField::kCount);
            }

            if (Has(Player::OnFootField::kPosition)) {
                Player::SerializePosition(bs, write, position);
            }

            if (Has(Player::OnFootField::kHeading)) {
                Player::SerializeAngle(bs, write, heading);
            }

            if (Has(Player::OnFootField::kVelocity)) {
                Player::SerializeVelocity(bs, write, velocity);
            }

            if (Has(Player::OnFootField::kInput)) {
                // The camera rides with the input, not with the pose: they are
                // one reading. Pair this tick's stick with last tick's view
                // and the player moves in a direction neither described.
                Player::SerializeStick(bs, write, leftStickX);
                Player::SerializeStick(bs, write, leftStickY);
                bs->Serialize(write, accelerator);
                bs->Serialize(write, brake);
                bs->Serialize(write, buttons);
                Player::SerializeAngle(bs, write, cameraRotation);
            }

            if (Has(Player::OnFootField::kFlags)) {
                if (write) {
                    bs->WriteBits(reinterpret_cast<const unsigned char *>(&flags), Player::PedFlag::kBits);
                }
                else {
                    flags = 0;
                    bs->ReadBits(reinterpret_cast<unsigned char *>(&flags), Player::PedFlag::kBits);
                }
            }
        }
    };
} // namespace VantaMP::Shared::RPC
