#pragma once

#include "shared/rpc/common/sequence.h"
#include "shared/rpc/common/wire_registry.h"

#include <mafianet/types.h>

#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <unordered_map>

namespace VantaMP::Shared {
    class PlayerEntity;
    class VehicleEntity;
}

namespace VantaMP::Shared::RPC {
    struct VehicleDamageReport;
    struct VehicleDriverSync;
    struct VehicleUnoccupiedSync;
} // namespace VantaMP::Shared::RPC

namespace VantaMP::Game {
    class PlayerManager;

    class VehicleManager final {
      public:
        explicit VehicleManager(PlayerManager &players);

        // heading is radians, matching Player.spawn. The quat form is the
        // full MTA rotation (script Vector3 degrees converted at the edge).
        std::uint64_t Create(std::uint32_t modelId, float x, float y, float z, float heading, std::uint8_t primaryColour, std::uint8_t secondaryColour,
                             std::uint8_t interior = 0, std::uint16_t dimension = 0);
        std::uint64_t Create(std::uint32_t modelId, float x, float y, float z, const glm::quat &rotation, std::uint8_t primaryColour, std::uint8_t secondaryColour,
                             std::uint8_t interior = 0, std::uint16_t dimension = 0);

        void SetInterior(Shared::VehicleEntity &vehicle, std::uint8_t interior);
        void SetDimension(Shared::VehicleEntity &vehicle, std::uint16_t dimension);

        void SetColors(Shared::VehicleEntity &vehicle, std::uint32_t c1, std::uint32_t c2, std::uint32_t c3, std::uint32_t c4);
        void SetPaintjob(Shared::VehicleEntity &vehicle, std::uint8_t paintjob);
        bool AddUpgrade(Shared::VehicleEntity &vehicle, std::uint16_t upgradeId);
        bool RemoveUpgrade(Shared::VehicleEntity &vehicle, std::uint16_t upgradeId);
        void RemoveAllUpgrades(Shared::VehicleEntity &vehicle);

        void Fix(Shared::VehicleEntity &vehicle);
        void Blow(Shared::VehicleEntity &vehicle);

        bool Destroy(std::uint64_t networkId);

        void HandleEnter(Shared::PlayerEntity &player, std::uint64_t vehicleId, std::uint8_t seat, bool warp, std::uint8_t door);
        void HandleExit(Shared::PlayerEntity &player, bool warp, std::uint8_t door, bool abortEnter = false, std::uint8_t doorRatio = 0, bool knockedOff = false);

        [[nodiscard]] Shared::PlayerEntity *OccupantOf(std::uint64_t vehicleId, std::uint8_t seat) const;
        // Takes the reporter so the report can be validated against them:
        // damage from a player who cannot see the vehicle is forged. Takes the
        // packet whole rather than a dozen loose parameters, because the mask
        // and the values must not be separable - the mask is what says which
        // of them mean anything.
        void HandleDamage(Shared::PlayerEntity &reporter, const Shared::RPC::VehicleDamageReport &report);

        // Last pose freezes: owner returns to the server. Anyone they were
        // sitting with is told they warped out.
        void HandleDisconnect(MafiaNet::PeerGuid guid);

        [[nodiscard]] Shared::VehicleEntity *Find(std::uint64_t networkId) const;

        // Motion from a client claiming to drive `vehicleId`. Validates
        // authority and values, then relays to everyone else. Returns false
        // when the packet was rejected (the reason is logged).
        bool HandleDriverSync(Shared::PlayerEntity &sender, const Shared::RPC::VehicleDriverSync &sync);

        // Tells one player, or everyone, the compact wire id for a vehicle.
        // A client cannot address motion for a vehicle until it has this.
        void AnnounceWireId(std::uint64_t networkId, MafiaNet::PeerGuid to = MafiaNet::UNASSIGNED_PEER_GUID);

        // Every live vehicle's wire id, for a client that has just joined.
        void AnnounceAllWireIds(MafiaNet::PeerGuid to);

        // Motion for vehicles nobody is driving, sent by their assigned
        // syncer. Validates per entry and relays the batch on.
        void HandleUnoccupiedSync(Shared::PlayerEntity &sender, const Shared::RPC::VehicleUnoccupiedSync &batch);

        // Re-elects the syncer for every unowned vehicle and sends the
        // start/stop handoffs. Cheap, and called on a slow cadence - a
        // vehicle nobody is near needs no syncer at all.
        void UpdateUnoccupiedSyncers();

      private:
        [[nodiscard]] std::uint8_t AssignSeat(std::uint64_t vehicleId, std::uint8_t requested, std::uint64_t requesterId) const;
        [[nodiscard]] Shared::PlayerEntity *FindOccupant(std::uint64_t vehicleId, std::uint8_t seat, std::uint64_t exceptId) const;
        void Vacate(Shared::PlayerEntity &player);

        PlayerManager &_players;
        std::unordered_map<std::uint64_t, Shared::VehicleEntity *> _vehicles;

        // THE SERVER IS THE ONLY MINTER. One registry, so two vehicles can
        // never hold the same slot and a released slot's generation is bumped
        // in exactly one place.
        Shared::RPC::Common::WireRegistry _wireIds;

        // Newest driver-sync sequence accepted per vehicle, so a reordered or
        // replayed datagram is dropped at the server rather than relayed on to
        // everyone.
        std::unordered_map<std::uint64_t, Shared::RPC::Sequence> _driverSequence;

        // Who is currently authoritative for each unowned vehicle's motion.
        // Absent means nobody is - which is the normal state for a car with
        // no player near it, and costs no bandwidth at all.
        std::unordered_map<std::uint64_t, MafiaNet::PeerGuid> _syncers;
    };
} // namespace VantaMP::Game
