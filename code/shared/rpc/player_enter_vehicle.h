#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // A player is getting into a vehicle.
    //
    // NOT ON THE SNAPSHOT. Entering is a transition that plays once: as a
    // flag it would restart the door animation every tick, and sent
    // unreliably it can be missed, leaving a player standing beside a car
    // everyone else sees them driving. MTA's VEHICLE_INOUT is the same idea.
    //
    // Seat 0 is the driver; 1-8 are passengers. The client that started the
    // climb leaves networkId at zero; the server stamps it and may reassign
    // the seat if the asked one is taken.
    struct PlayerEnterVehicle {
        static constexpr const char *kIdentifier = "VantaMP::PlayerEnterVehicle";

        static constexpr std::uint8_t kMaxSeat = 8;

        std::uint64_t networkId = 0;
        std::uint64_t vehicleId = 0;
        std::uint8_t  seat      = 0;
        bool          warp      = false;
        // GTA eTargetDoor (0 / 8 / 9 / 11). 0xFF: the receiver picks one.
        // MTA's VEHICLE_INOUT carries the same door so remotes walk to the
        // side the owner used - that is what starts the passenger-to-driver
        // shuffle. Without it they path to the nearest door from wherever
        // they are and freeze mid-walk, or sit in the passenger seat.
        std::uint8_t door = 0xFF;

        void FromParameters(std::uint64_t id, std::uint64_t vehicle, std::uint8_t seatNumber, bool instant, std::uint8_t usedDoor = 0xFF) {
            networkId = id;
            vehicleId = vehicle;
            seat      = seatNumber;
            warp      = instant;
            door      = usedDoor;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, networkId);
            bs->Serialize(write, vehicleId);
            bs->Serialize(write, seat);
            bs->Serialize(write, warp);
            bs->Serialize(write, door);
        }
    };
} // namespace VantaMP::Shared::RPC
