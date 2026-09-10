#pragma once

#include <cstdint>

namespace VantaMP::Game {
    void UpdateVehicleSync();
    void ClearRemoteVehicles();

    // MTA enter_passenger (G). Edge-detect the key at the call site.
    void TryLocalPassengerKey();

    void ApplyRemoteEnterVehicle(std::uint64_t networkId, std::uint64_t vehicleId, std::uint8_t seat, bool warp, std::uint8_t door);
    void ApplyRemoteExitVehicle(std::uint64_t networkId, bool warp, std::uint8_t door, bool abortEnter = false, std::uint8_t doorRatio = 0, bool knockedOff = false);
    void ApplyOccupancyAbort(std::uint64_t networkId, std::uint64_t vehicleId, std::uint8_t kind, std::uint8_t seat, std::uint8_t door);
    void ApplyRemoteVehicleDamage(std::uint64_t vehicleId, std::uint8_t engine, std::uint64_t doors, std::uint32_t wheels, std::uint32_t panels, std::uint32_t lights,
                                  std::uint64_t doorRatios, bool applyRatios, float health);

    // The replica id for a spawned CVehicle, or 0 if it is not ours.
    [[nodiscard]] std::uint64_t VehicleNetworkIdOf(const void *vehicle);

    // lateJoin is MTA's stream-in path: the ped already exists in the
    // seat on every other client, so they warp. A live enter is the
    // PlayerEnterVehicle RPC, which walks them in.
    void SeatRemoteInVehicle(int slot, std::uint64_t vehicleId, std::uint8_t seat, bool lateJoin);

    void ClearRemoteOccupancy(int slot);

    // Script set on a car this client currently owns (the driver).
    void ApplyOwnedVehicleWorldSpace(std::uint64_t networkId, std::uint8_t interior, std::uint16_t dimension, std::uint8_t flags);

    [[nodiscard]] void *VehicleHandleOf(std::uint64_t networkId);
} // namespace VantaMP::Game
