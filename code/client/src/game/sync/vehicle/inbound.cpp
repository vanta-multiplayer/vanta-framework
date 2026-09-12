#include "game/sync/vehicle_sync.h"

#include "game/sync/vehicle/state.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/rpc/common/sequence.h"
#include "shared/rpc/vehicle/driver_sync.h"
#include "shared/rpc/vehicle/unoccupied_sync.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

// WHAT THIS CLIENT RECEIVES ABOUT VEHICLES.
//
// Decodes the motion packets into the same VehicleEntity fields the type
// handlers already read, so BikeSync and AutomobileSync are untouched by where
// the state came from - that separation is the whole point of the design.
//
// LATEST WINS: the streams are Unreliable, so datagrams reorder. Anything
// older than the newest applied sequence is dropped rather than applied, or a
// late packet drags the vehicle backwards.
//
// Split out of vehicle_sync.cpp. Shared registry state is in vehicle/state.h.
namespace VantaMP::Game {
    using namespace VehicleSyncState;

    void AdoptVehicleWireId(std::uint64_t networkId, std::uint16_t wireId) {
        gWireIds.Adopt(networkId, Shared::RPC::Common::WireId {wireId});
    }

    void SetVehicleSyncerRole(std::uint64_t networkId, bool start) {
        auto &remote = gVehicles[networkId];

        if (remote.isSyncer == start) {
            return;
        }

        remote.isSyncer = start;

        // A ROLE CHANGE RESETS THE BASELINE. Deltas are compared against what
        // this client last sent, and it has sent nothing as syncer for a
        // vehicle it has just been handed - so without this the first batch
        // would suppress every field that happens to match a stale baseline
        // from the last time it held the role, and the new receivers would
        // never be told the current state.
        remote.haveSynced = false;
    }

    void ApplyRemoteUnoccupiedSync(const Shared::RPC::VehicleUnoccupiedSync &batch) {
        namespace Field = Shared::RPC::Vehicle::UnoccupiedField;

        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication) {
            return;
        }

        for (const auto &entry : batch.entries) {
            const auto networkId = gWireIds.Resolve(entry.vehicleId);

            if (networkId == 0) {
                continue;
            }

            auto *entity = dynamic_cast<Shared::VehicleEntity *>(replication->GetEntityByNetworkID(static_cast<MafiaNet::NetworkID>(networkId)));

            if (!entity || entity->IsOwner()) {
                continue;
            }

            auto it = gVehicles.find(networkId);

            // Our own batch comes back to us for vehicles we sync; applying it
            // would fight the local simulation.
            if (it == gVehicles.end() || it->second.isSyncer) {
                continue;
            }

            if (entry.Has(Field::kPosition)) {
                entity->position = entry.position;
            }

            if (entry.Has(Field::kRotation)) {
                entity->rotation = entry.rotation;
            }

            if (entry.Has(Field::kVelocity)) {
                entity->velocity = entry.velocity;
            }

            if (entry.Has(Field::kTurnSpeed)) {
                entity->angularVelocity = entry.turnSpeed;
            }

            if (entry.Has(Field::kHealth)) {
                entity->health = entry.health;
            }
        }
    }

    void ApplyRemoteDriverSync(const Shared::RPC::VehicleDriverSync &sync) {
        namespace Field = Shared::RPC::Vehicle::DriverField;

        const auto networkId = gWireIds.Resolve(sync.vehicleId);

        // A wire id that does not resolve is a packet for a vehicle that has
        // despawned, or one minted before a slot was reused. Dropping it is
        // the point of the generation half of the id - the alternative is
        // steering whatever now occupies that slot.
        if (networkId == 0) {
            return;
        }

        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication) {
            return;
        }

        auto *entity = dynamic_cast<Shared::VehicleEntity *>(replication->GetEntityByNetworkID(static_cast<MafiaNet::NetworkID>(networkId)));

        if (!entity) {
            return;
        }

        // NEVER LET A PACKET OVERWRITE WHAT WE OURSELVES OWN. The server
        // broadcasts, so our own sync comes back to us; applying it would
        // fight the local simulation.
        if (entity->IsOwner()) {
            return;
        }

        auto it = gVehicles.find(networkId);

        if (it == gVehicles.end()) {
            return;
        }

        auto &remote = it->second;

        // LATEST WINS. The stream is Unreliable, so datagrams reorder: an
        // older snapshot arriving after a newer one must be dropped, not
        // applied, or the car visibly jumps backwards. Signed-difference
        // comparison so the uint16 wrap is harmless.
        if (remote.haveAppliedSequence && !Shared::RPC::IsNewerSequence(sync.sequence, remote.appliedSequence)) {
            return;
        }

        remote.appliedSequence     = sync.sequence;
        remote.haveAppliedSequence = true;

        // ONLY WHAT THE MASK NAMES. A field the sender suppressed keeps the
        // value we already have - it is NOT zero, and it is NOT stale: the
        // sender suppressed it precisely because it had not changed since the
        // value we are holding.
        if (sync.Has(Field::kPosition)) {
            entity->position = sync.position;
        }

        if (sync.Has(Field::kRotation)) {
            entity->rotation = sync.rotation;
        }

        if (sync.Has(Field::kVelocity)) {
            entity->velocity = sync.velocity;
        }

        if (sync.Has(Field::kTurnSpeed)) {
            entity->angularVelocity = sync.turnSpeed;
        }

        if (sync.Has(Field::kHealth)) {
            entity->health = sync.health;
        }

        if (sync.Has(Field::kControls)) {
            entity->steer       = sync.steer;
            entity->accelerator = sync.accelerator;
            entity->brake       = sync.brake;
            entity->gasPedal    = sync.gasPedal;
            entity->brakePedal  = sync.brakePedal;
        }

        if (sync.Has(Field::kFlags)) {
            namespace Flag = Shared::RPC::Vehicle::VehicleFlag;

            entity->onGround  = (sync.flags & Flag::kOnGround) != 0;
            entity->engineOn  = (sync.flags & Flag::kEngineOn) != 0;
            entity->sirenOn   = (sync.flags & Flag::kSirenOn) != 0;
            entity->handbrake = (sync.flags & Flag::kHandbrake) != 0;
            entity->lightsOn  = (sync.flags & Flag::kLightsOn) != 0;
        }

        // ApplyRemoteVehicle runs from the per-frame tick and picks the new
        // pose up from the entity, exactly as it did when the replica carried
        // it. Nothing below the dispatcher knows this came from a packet -
        // which is the whole point of decoding into the entity.
    }
} // namespace VantaMP::Game
