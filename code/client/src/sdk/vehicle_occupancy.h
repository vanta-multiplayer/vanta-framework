#pragma once

#include <cstdint>

namespace VantaMP::SDK {
    inline constexpr std::uint8_t kDriverSeat           = 0;
    inline constexpr std::uint8_t kMaxPassengerSeats    = 8;
    inline constexpr std::uint8_t kUnknownPassengerCount = 255;
    inline constexpr std::uint8_t kInvalidSeat          = 0xFF;
    inline constexpr std::uint8_t kInvalidDoor          = 0xFF;

    // Slot 0 is the local player. Remote peds use the slot they occupy.
    struct PedOccupancy {
        void        *vehicle  = nullptr;
        std::uint8_t seat     = 0;
        std::uint8_t door     = kInvalidDoor;
        bool         seated   = false;
        bool         entering = false;
        bool         leaving  = false;
    };

    [[nodiscard]] PedOccupancy QueryPedOccupancy(int slot);

    [[nodiscard]] bool IsPedInVehicle(int slot);
    [[nodiscard]] bool IsPedEnteringVehicle(int slot);
    [[nodiscard]] bool IsPedLeavingVehicle(int slot);

    // Seated, walking to a door, or climbing out. Any of these means the
    // on-foot interpolator must leave the ped alone.
    [[nodiscard]] bool IsPedOccupyingVehicle(int slot);

    bool EnterVehicle(int slot, void *vehicle, std::uint8_t seat, bool warp, std::uint8_t door = kInvalidDoor);
    void ExitVehicle(int slot, bool warp, std::uint8_t door = kInvalidDoor);

    // MTA / old Vanta passenger reach. Measured from the nearest passenger
    // door, not the vehicle centre - standing at the back of a bus is close
    // to a door and far from the middle.
    inline constexpr float kPassengerReach = 20.0f;

    // Distance from the ped to that vehicle's nearest passenger door.
    // False when the vehicle is unusable (no model, wrecked) so the caller
    // does not have to repeat those tests.
    [[nodiscard]] bool PassengerDoorDistance(int slot, void *vehicle, float *outDistance);

    // First free passenger seat (1-based), or 0 when the car is full.
    // m_nMaxPassengers == 255 means unknown, not 255 seats.
    [[nodiscard]] std::uint8_t FreePassengerSeat(const void *vehicle);

    // Someone is walking in as driver. The occupant is being jacked -
    // GTA is dragging them out. Starting LeaveCar on them fights that.
    [[nodiscard]] bool IsAnyoneEnteringVehicleAsDriver(const void *vehicle);

    // CTaskComplexEnterCar holds a raw CVehicle*. Cancel before delete.
    void CancelEntriesInto(void *vehicle);

    // Warp-exit plus explicit pointer clear. Call before the ped is freed.
    void UnlinkFromVehicle(int slot);

    // Take every Vanta occupant out of this vehicle and drop the seat
    // pointers. World::Remove on a car that still names a CPlayerPed is
    // how a disconnect (or a streamed-out wreck) takes the client down
    // with it. killOccupants writes health to zero first so an explosion
    // still counts as a death after the seats are empty.
    void EvacuateVehicle(void *vehicle, bool killOccupants = false);

    // Called for the local player when EvacuateVehicle kills them. The
    // death packet needs a weapon type; occupancy does not know about
    // the network.
    void SetVehicleExplosionDeathHandler(void (*handler)());
} // namespace VantaMP::SDK
