#pragma once

#include <mafianet/types.h>

#include <cstdint>
#include <unordered_map>

namespace VantaMP::Shared {
    class PlayerEntity;
    class VehicleEntity;
}

namespace VantaMP::Game {
    class PlayerManager;

    class VehicleManager final {
      public:
        explicit VehicleManager(PlayerManager &players);

        // heading is radians, matching Player.spawn.
        std::uint64_t Create(std::uint32_t modelId, float x, float y, float z, float heading, std::uint8_t primaryColour, std::uint8_t secondaryColour,
                             std::uint8_t interior = 0, std::uint16_t dimension = 0);

        void SetInterior(Shared::VehicleEntity &vehicle, std::uint8_t interior);
        void SetDimension(Shared::VehicleEntity &vehicle, std::uint16_t dimension);

        void SetColors(Shared::VehicleEntity &vehicle, std::uint32_t c1, std::uint32_t c2, std::uint32_t c3, std::uint32_t c4);
        void SetPaintjob(Shared::VehicleEntity &vehicle, std::uint8_t paintjob);
        bool AddUpgrade(Shared::VehicleEntity &vehicle, std::uint16_t upgradeId);
        bool RemoveUpgrade(Shared::VehicleEntity &vehicle, std::uint16_t upgradeId);
        void RemoveAllUpgrades(Shared::VehicleEntity &vehicle);

        bool Destroy(std::uint64_t networkId);

        void HandleEnter(Shared::PlayerEntity &player, std::uint64_t vehicleId, std::uint8_t seat, bool warp, std::uint8_t door);
        void HandleExit(Shared::PlayerEntity &player, bool warp, std::uint8_t door);
        void HandleDamage(std::uint64_t vehicleId, std::uint8_t engine, std::uint64_t doors, std::uint32_t wheels, std::uint32_t panels, std::uint32_t lights,
                          std::uint64_t doorRatios, bool applyRatios, float health);

        // Last pose freezes: owner returns to the server. Anyone they were
        // sitting with is told they warped out.
        void HandleDisconnect(MafiaNet::PeerGuid guid);

        [[nodiscard]] Shared::VehicleEntity *Find(std::uint64_t networkId) const;

      private:
        [[nodiscard]] std::uint8_t AssignSeat(std::uint64_t vehicleId, std::uint8_t requested, std::uint64_t requesterId) const;
        [[nodiscard]] Shared::PlayerEntity *FindOccupant(std::uint64_t vehicleId, std::uint8_t seat, std::uint64_t exceptId) const;
        void Vacate(Shared::PlayerEntity &player);

        PlayerManager &_players;
        std::unordered_map<std::uint64_t, Shared::VehicleEntity *> _vehicles;
    };
} // namespace VantaMP::Game
