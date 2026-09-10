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

    // Drop an enter walk and stand them back up. Movement cancel and a
    // script veto both land here — GTA will not abort CTaskComplexEnterCar
    // just because the stick moved.
    void AbortVehicleEnter(int slot);

    // Stop a leave and keep the seat. If they already stepped out, warp
    // them back so a refused vehicleStartExit does not dump them on the road.
    void AbortVehicleExit(int slot, void *vehicle, std::uint8_t seat);

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
    // GTA's enter/leave door target id (0/8/9/10/11) -> the CDoor array index
    // (2-5) that damage and ratio code uses. kInvalidDoor if it is not one of
    // the four climb-through doors.
    [[nodiscard]] std::uint8_t DoorRatioIndexForTarget(std::uint8_t target);

    // Bit i set = CDoor i is being driven by a ped's enter/leave animation
    // right now, so the network must not write a ratio to it. Covers the
    // local player and every remote ped, which is the half that was missing.
    [[nodiscard]] std::uint8_t OccupiedDoorMask(void *vehicle);

    void CancelEntriesInto(void *vehicle);

    // Pointer detach only. SetPedOut re-enters the car; a wreck, a
    // corpse, or a disconnect mid-explode is the crash. Gameplay exits
    // still go through ExitVehicle.
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
