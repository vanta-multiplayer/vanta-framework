#include "vehicle_manager.h"

#include "game/player_manager.h"
#include "scripting/bindings.h"
#include "shared/entity/player_entity.h"
#include "shared/rpc/player/enter_vehicle.h"
#include "shared/rpc/player/exit_vehicle.h"
#include "shared/rpc/player/occupancy_abort.h"
#include "shared/rpc/vehicle/authority.h"
#include "shared/rpc/vehicle/damage.h"
#include "shared/rpc/vehicle/driver_sync.h"
#include "shared/rpc/common/metrics.h"
#include "shared/rpc/vehicle/sync_owner.h"
#include "shared/rpc/vehicle/unoccupied_sync.h"
#include "shared/catalog/model_catalog.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/vehicle/validate.h"
#include "shared/vehicle/rotation.h"
#include "shared/vehicle/upgrades.h"
#include "shared/rpc/world_space.h"
#include "shared/world/world_space.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>

#include <iterator>

namespace VantaMP::Game {
    namespace {
        void BroadcastAbort(std::uint64_t playerId, std::uint64_t vehicleId, std::uint8_t kind, std::uint8_t seat, std::uint8_t door) {
            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            Shared::RPC::PlayerOccupancyAbort abort;
            abort.FromParameters(playerId, vehicleId, kind, seat, door);
            net->BroadcastRPC(abort, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
    } // namespace

    VehicleManager::VehicleManager(PlayerManager &players) : _players(players) {}

    std::uint64_t VehicleManager::Create(std::uint32_t modelId, float x, float y, float z, float heading, std::uint8_t primaryColour, std::uint8_t secondaryColour,
                                         std::uint8_t interior, std::uint16_t dimension) {
        return Create(modelId, x, y, z, Shared::QuatFromHeadingRadians(heading), primaryColour, secondaryColour, interior, dimension);
    }

    std::uint64_t VehicleManager::Create(std::uint32_t modelId, float x, float y, float z, const glm::quat &rotation, std::uint8_t primaryColour,
                                         std::uint8_t secondaryColour, std::uint8_t interior, std::uint16_t dimension) {
        if (!Shared::ModelCatalog::IsVehicle(modelId)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Refusing vehicle model {}; not in the catalog", modelId);
            return 0;
        }

        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication) {
            return 0;
        }

        auto *vehicle = replication->CreateEntity<Shared::VehicleEntity>();

        if (!vehicle) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Could not create a vehicle entity");
            return 0;
        }

        vehicle->modelId         = modelId;
        vehicle->primaryColour   = primaryColour;
        vehicle->secondaryColour = secondaryColour;
        vehicle->FillColoursFromPalette();
        vehicle->position        = glm::vec3(x, y, z);
        vehicle->rotation        = rotation;
        vehicle->health          = 1000.0f;
        vehicle->engineOn        = false;
        vehicle->blown           = false;
        vehicle->interior        = interior;
        vehicle->dimension       = dimension;

        const auto id = static_cast<std::uint64_t>(vehicle->GetNetworkID());

        _vehicles[id] = vehicle;

        // Mint and announce the compact id before anyone can drive it. A
        // client with no mapping falls back to plain replication, which is
        // correct but costs the transform channel, so there is no reason to
        // make it wait.
        AnnounceWireId(id);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Created vehicle {} (model {}) at {:.1f}, {:.1f}, {:.1f}", id, modelId, x, y, z);

        return id;
    }

    bool VehicleManager::Destroy(std::uint64_t networkId) {
        const auto found = _vehicles.find(networkId);

        if (found == _vehicles.end()) {
            return false;
        }

        _players.ForEach([networkId](Shared::PlayerEntity *player) {
            if (player->vehicleNetworkId == networkId) {
                player->vehicleNetworkId = 0;
                player->vehicleSeat      = 0;
            }
        });

        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->DestroyEntity(found->second);
        }

        // RELEASE BUMPS THE SLOT'S GENERATION, which is what stops a motion
        // packet still in flight for this vehicle from addressing whatever
        // takes the slot next. Released here rather than lazily, because the
        // window between despawn and reuse is exactly where the collision
        // would live.
        _wireIds.Release(networkId);
        _driverSequence.erase(networkId);
        _syncers.erase(networkId);

        _vehicles.erase(found);
        return true;
    }

    std::uint8_t VehicleManager::AssignSeat(std::uint64_t vehicleId, std::uint8_t requested, std::uint64_t requesterId) const {
        bool taken[Shared::VehicleEntity::kMaxSeat + 1] {};

        _players.ForEach([&](Shared::PlayerEntity *player) {
            if (static_cast<std::uint64_t>(player->GetNetworkID()) == requesterId) {
                return;
            }

            if (player->vehicleNetworkId != vehicleId || player->vehicleSeat > Shared::VehicleEntity::kMaxSeat) {
                return;
            }

            taken[player->vehicleSeat] = true;
        });

        // F is driver, G is passenger. A taken seat is a refusal, not a
        // different cushion. Reassigning used to turn a jack into a
        // passenger warp and crash the other client.
        if (requested <= Shared::VehicleEntity::kMaxSeat && !taken[requested]) {
            return requested;
        }

        return Shared::RPC::PlayerEnterVehicle::kMaxSeat + 1;
    }

    Shared::PlayerEntity *VehicleManager::OccupantOf(std::uint64_t vehicleId, std::uint8_t seat) const {
        return FindOccupant(vehicleId, seat, 0);
    }

    Shared::PlayerEntity *VehicleManager::FindOccupant(std::uint64_t vehicleId, std::uint8_t seat, std::uint64_t exceptId) const {
        Shared::PlayerEntity *occupant = nullptr;

        _players.ForEach([&](Shared::PlayerEntity *player) {
            if (static_cast<std::uint64_t>(player->GetNetworkID()) == exceptId) {
                return;
            }

            if (player->vehicleNetworkId == vehicleId && player->vehicleSeat == seat) {
                occupant = player;
            }
        });

        return occupant;
    }

    void VehicleManager::Vacate(Shared::PlayerEntity &player) {
        player.vehicleNetworkId = 0;
        player.vehicleSeat      = 0;
    }

    void VehicleManager::HandleEnter(Shared::PlayerEntity &player, std::uint64_t vehicleId, std::uint8_t seat, bool warp, std::uint8_t door) {
        auto *vehicle = Find(vehicleId);

        if (!vehicle) {
            return;
        }

        const auto playerId = static_cast<std::uint64_t>(player.GetNetworkID());
        auto *net            = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        if (vehicle->locked && player.vehicleNetworkId != vehicleId) {
            BroadcastAbort(playerId, vehicleId, Shared::RPC::PlayerOccupancyAbort::kEnter, seat, door);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Refusing enter — vehicle {} is locked", vehicleId);
            return;
        }

        const bool jacked = seat == 0 && FindOccupant(vehicleId, 0, playerId) != nullptr;

        if (!warp) {
            if (!Scripting::EmitVehicleStartEnter(playerId, vehicleId, seat, jacked, door)) {
                BroadcastAbort(playerId, vehicleId, Shared::RPC::PlayerOccupancyAbort::kEnter, seat, door);
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("vehicleStartEnter vetoed {} into {} seat {}", playerId, vehicleId, seat);
                return;
            }
        }

        // MTA VEHICLE_REQUEST_JACK: F always asks for seat 0. If that
        // seat is taken, the enter is a jack - EnterCarAsDriver with
        // whatever door they walked to, including the passenger side.
        // Occupancy stays on the driver until the jacker sits, or GTA
        // pulls them and they send exit themselves.
        if (auto *occupant = FindOccupant(vehicleId, seat, playerId)) {
            if (seat != 0) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Seat {} on vehicle {} is taken", seat, vehicleId);
                return;
            }

            if (!warp) {
                Shared::RPC::PlayerEnterVehicle enter;
                enter.FromParameters(playerId, vehicleId, 0, false, door);
                net->BroadcastRPC(enter, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Player {} is jacking vehicle {} through door {}", playerId, vehicleId, door);
                return;
            }

            Vacate(*occupant);
        }

        const auto assigned = AssignSeat(vehicleId, seat, playerId);

        if (assigned > Shared::VehicleEntity::kMaxSeat) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("No free seat on vehicle {} for {}", vehicleId, playerId);
            return;
        }

        if (player.vehicleNetworkId != 0 && player.vehicleNetworkId != vehicleId) {
            Vacate(player);
        }

        player.vehicleNetworkId = vehicleId;
        player.vehicleSeat      = assigned;

        // AUTHORITY IS THE DRIVER SEAT, NOT "BEING IN THE CAR".
        //
        // Seat 0 claims the car. Any other seat must not - and, more than
        // that, taking a passenger seat has to REVOKE authority this player
        // still held from having driven this same car earlier. The owner
        // survives an exit on purpose (an empty car still rolls), so without
        // this a player who drove, got out, and climbed back into a
        // passenger seat kept ownership: their client stayed the one pushing
        // pose, rotation and pad, and they drove the car from the back seat
        // while the real driver fought them for it.
        if (assigned == 0) {
            vehicle->SetOwner(player.ownerGUID);
        }
        else if (vehicle->ownerGUID == player.ownerGUID) {
            auto *driver = FindOccupant(vehicleId, 0, playerId);

            vehicle->SetOwner(driver ? driver->ownerGUID : MafiaNet::UNASSIGNED_PEER_GUID);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)
                ->debug("Player {} took seat {} on vehicle {}; authority revoked and handed to {}", playerId, assigned, vehicleId,
                        driver ? static_cast<std::uint64_t>(driver->GetNetworkID()) : 0);
        }

        Shared::RPC::PlayerEnterVehicle enter;
        enter.FromParameters(playerId, vehicleId, assigned, warp, door);
        net->BroadcastRPC(enter, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);

        if (warp) {
            Scripting::EmitVehicleEnter(playerId, vehicleId, assigned);
        }

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Player {} entered vehicle {} seat {} (asked {})", playerId, vehicleId, assigned, seat);
    }

    void VehicleManager::HandleExit(Shared::PlayerEntity &player, bool warp, std::uint8_t door, bool abortEnter, std::uint8_t doorRatio, bool knockedOff) {
        const auto playerId  = static_cast<std::uint64_t>(player.GetNetworkID());
        const auto vehicleId = player.vehicleNetworkId;
        const auto seat      = player.vehicleSeat;

        if (!abortEnter && vehicleId != 0 && !warp) {
            if (!Scripting::EmitVehicleStartExit(playerId, vehicleId, seat, door)) {
                BroadcastAbort(playerId, vehicleId, Shared::RPC::PlayerOccupancyAbort::kExit, seat, door);
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("vehicleStartExit vetoed {} from {} seat {}", playerId, vehicleId, seat);
                return;
            }
        }

        Vacate(player);

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        // Broadcast even when they were not seated. A jack walk never
        // took the seat; remotes still have to abort the enter task.
        Shared::RPC::PlayerExitVehicle exit;
        exit.FromParameters(playerId, warp, door, abortEnter, doorRatio, knockedOff);
        net->BroadcastRPC(exit, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);

        if (!abortEnter && vehicleId != 0) {
            Scripting::EmitVehicleExit(playerId, vehicleId, seat);
        }
    }

    void VehicleManager::HandleDisconnect(MafiaNet::PeerGuid guid) {
        auto *player = _players.Find(guid);

        if (!player) {
            return;
        }

        if (player->vehicleNetworkId != 0) {
            const auto playerId = static_cast<std::uint64_t>(player->GetNetworkID());
            const auto vehicleId = player->vehicleNetworkId;
            const auto seat      = player->vehicleSeat;

            Vacate(*player);

            if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
                Shared::RPC::PlayerExitVehicle exit;
                exit.FromParameters(playerId, true, 0xFF, false);
                net->BroadcastRPC(exit, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }

            Scripting::EmitVehicleExit(playerId, vehicleId, seat);
        }

        for (auto &[id, vehicle] : _vehicles) {
            if (vehicle && vehicle->ownerGUID == guid) {
                vehicle->SetOwner(MafiaNet::UNASSIGNED_PEER_GUID);
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Vehicle {} returned to the server; its owner disconnected", id);
            }
        }

        // A DEPARTED PLAYER MUST NOT STAY THE SYNCER. Nothing would arrive
        // for the vehicles they held, and the next election would compare
        // against a guid that no longer exists. Dropping the assignment here
        // lets the next sweep re-elect from whoever is actually present.
        for (auto it = _syncers.begin(); it != _syncers.end();) {
            it = it->second == guid ? _syncers.erase(it) : std::next(it);
        }
    }

    void VehicleManager::HandleDamage(Shared::PlayerEntity &reporter, const Shared::RPC::VehicleDamageReport &report) {
        namespace Field = Shared::RPC::Vehicle::DamageField;

        auto *vehicle = Find(report.vehicleId);

        if (!vehicle) {
            return;
        }

        // A REPORTER MUST BE ABLE TO SEE WHAT THEY ARE REPORTING.
        //
        // This check used to be "is the sender a connected player", which is
        // no check at all: any client could damage, wreck or pop the doors of
        // any vehicle anywhere on the map, and the server would merge it and
        // broadcast it to everyone as authoritative.
        //
        // The radius is deliberately generous - this is an anti-forgery
        // guard, not a simulation rule. A bystander who shot a car from
        // across a street is legitimate; one reporting a car in another city
        // is not. The vehicle's own occupants and owner are always in range
        // by definition, so they are not distance-tested.
        const bool isOccupant = reporter.vehicleNetworkId == report.vehicleId;
        const bool isOwner    = vehicle->ownerGUID == reporter.ownerGUID;

        if (!isOccupant && !isOwner) {
            constexpr float kMaxReportDistance = 300.0f;

            const glm::vec3 delta = reporter.position - vehicle->position;

            if (!Shared::IsValidPosition(reporter.position) || glm::dot(delta, delta) > kMaxReportDistance * kMaxReportDistance) {
                return;
            }
        }

        // OMITTED MEANS DO NOT MODIFY, AND HERE THAT IS THE WHOLE POINT.
        //
        // The merge below is worse-wins. A group the reporter did not send
        // would, if it were read as zero, merge as "undamaged" - and because
        // worse-wins takes the larger value, a zero cannot repair anything
        // by itself. But doorRatios is REPLACED rather than merged, so an
        // absent ratios group read as zero would slam every door shut.
        bool changed = false;

        if (report.Has(Field::kEngine)) {
            vehicle->damageEngine = report.damageEngine > vehicle->damageEngine ? report.damageEngine : vehicle->damageEngine;
            changed               = true;
        }

        if (report.Has(Field::kDoors)) {
            vehicle->doorsPacked = Shared::VehicleEntity::WorseBytes(vehicle->doorsPacked, report.doorsPacked, Shared::VehicleEntity::kDoorCount);
            changed              = true;
        }

        if (report.Has(Field::kWheels)) {
            vehicle->wheelsPacked =
                static_cast<std::uint32_t>(Shared::VehicleEntity::WorseBytes(vehicle->wheelsPacked, report.wheelsPacked, Shared::VehicleEntity::kWheelCount));
            changed = true;
        }

        if (report.Has(Field::kPanels)) {
            vehicle->panels = Shared::VehicleEntity::WorsePacked(vehicle->panels, report.panels, Shared::VehicleEntity::kPanelCount, 4);
            changed         = true;
        }

        if (report.Has(Field::kLights)) {
            vehicle->lights = Shared::VehicleEntity::WorsePacked(vehicle->lights, report.lights, Shared::VehicleEntity::kLightCount, 2);
            changed         = true;
        }

        if (report.Has(Field::kHealth)) {
            // Health arrives from a client. A NaN merged here would propagate
            // into the replica and from there into every receiver's GTA
            // vehicle, which never recovers.
            if (!Shared::RPC::Vehicle::IsValidHealth(report.health)) {
                return;
            }

            vehicle->health = report.health < vehicle->health ? report.health : vehicle->health;
            vehicle->LatchBlown();
            changed = true;
        }

        // REPLACE, DO NOT MAX - and only when nobody is driving.
        // A passenger report used to stomp the owner's close, then the
        // owner pushed shut, then the passenger opened it again: that
        // is the door slamming on a 100ms beat.
        bool hasDriver = false;

        _players.ForEach([&](Shared::PlayerEntity *player) {
            if (player->vehicleNetworkId == report.vehicleId && player->vehicleSeat == 0) {
                hasDriver = true;
            }
        });

        const bool takeRatios = report.Has(Field::kDoorRatios) && !hasDriver;

        if (takeRatios) {
            vehicle->doorRatiosPacked = report.doorRatiosPacked;
            changed                   = true;
        }

        if (!changed) {
            return;
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        // Rebroadcast the MERGED authoritative state, masked to what actually
        // moved. The server is the source of truth here, not the reporter:
        // two clients can each see half a wreck, and everyone must end up
        // with the union.
        Shared::RPC::VehicleDamageReport merged;

        merged.vehicleId = report.vehicleId;

        if (report.Has(Field::kEngine)) {
            merged.Mark(Field::kEngine);
            merged.damageEngine = vehicle->damageEngine;
        }

        if (report.Has(Field::kDoors)) {
            merged.Mark(Field::kDoors);
            merged.doorsPacked = vehicle->doorsPacked;
        }

        if (report.Has(Field::kWheels)) {
            merged.Mark(Field::kWheels);
            merged.wheelsPacked = vehicle->wheelsPacked;
        }

        if (report.Has(Field::kPanels)) {
            merged.Mark(Field::kPanels);
            merged.panels = vehicle->panels;
        }

        if (report.Has(Field::kLights)) {
            merged.Mark(Field::kLights);
            merged.lights = vehicle->lights;
        }

        if (report.Has(Field::kHealth)) {
            merged.Mark(Field::kHealth);
            merged.health = vehicle->health;
        }

        if (takeRatios) {
            merged.Mark(Field::kDoorRatios);
            merged.doorRatiosPacked = vehicle->doorRatiosPacked;
        }

        net->BroadcastRPC(merged, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);

        MafiaNet::BitStream sizing;

        merged.Serialize(&sizing, true);
        Shared::RPC::Metrics::Counters::Get().Record(Shared::RPC::Metrics::PacketType::Damage, sizing.GetNumberOfBytesUsed(),
                                                    static_cast<std::uint32_t>(_players.Count()));
    }


    void VehicleManager::AnnounceWireId(std::uint64_t networkId, MafiaNet::PeerGuid to) {
        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        const auto id = _wireIds.Acquire(networkId);

        if (!id.Valid()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Wire id space exhausted; vehicle {} stays on plain replication", networkId);
            return;
        }

        Shared::RPC::VehicleSyncOwner message;

        // start=false: this is an id announcement, not a syncer handoff. The
        // two share a packet because they share a trigger and a recipient;
        // see the note in sync_owner.h.
        message.FromParameters(networkId, false, 0, id.value);

        if (to == MafiaNet::UNASSIGNED_PEER_GUID) {
            net->BroadcastRPC(message, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
        else {
            net->SendRPC(message, MafiaNet::ToGuid(to), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }

        Shared::RPC::Metrics::Counters::Get().Record(Shared::RPC::Metrics::PacketType::SyncOwner, 11);
    }

    void VehicleManager::AnnounceAllWireIds(MafiaNet::PeerGuid to) {
        for (const auto &[networkId, vehicle] : _vehicles) {
            if (vehicle) {
                AnnounceWireId(networkId, to);
            }
        }
    }

    bool VehicleManager::HandleDriverSync(Shared::PlayerEntity &sender, const Shared::RPC::VehicleDriverSync &sync) {
        namespace Auth = Shared::RPC::Vehicle;

        const auto networkId = _wireIds.Resolve(sync.vehicleId);

        if (networkId == 0) {
            return false;
        }

        auto *vehicle = Find(networkId);

        // AUTHORITY FIRST, VALUES SECOND. A packet from someone with no claim
        // on this vehicle is not worth validating.
        //
        // The seat test is what stops a PASSENGER driving the car. Owning the
        // entity is not enough on its own: the server deliberately keeps
        // ownership across an exit so an empty car still rolls, so a player
        // who drove, got out and climbed into a passenger seat would still
        // pass an ownership-only check. That was a real bug once and this is
        // where it must not come back.
        const bool seated     = sender.vehicleNetworkId == networkId;
        const bool isSeatZero = seated && sender.vehicleSeat == 0;
        const auto verdict    = Auth::CheckDriverAuthority(vehicle != nullptr, vehicle && vehicle->ownerGUID == sender.ownerGUID, isSeatZero, seated);

        if (verdict != Auth::Reject::None) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)
                ->debug("Rejected driver sync for vehicle {} from {}: {}", networkId, static_cast<std::uint64_t>(sender.GetNetworkID()),
                        Auth::RejectReason(verdict));
            return false;
        }

        // NaN, Inf and out-of-world values, checked only on the fields the
        // mask actually claims to carry.
        if (const auto bad = Auth::ValidateDriverSync(sync); bad != Auth::Reject::None) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)
                ->warn("Dropped driver sync for vehicle {} from {}: {}", networkId, static_cast<std::uint64_t>(sender.GetNetworkID()), Auth::RejectReason(bad));
            return false;
        }

        // Drop a reordered or replayed datagram here, so it is never relayed.
        auto &newest = _driverSequence[networkId];

        if (newest != 0 && !Shared::RPC::IsNewerSequence(sync.sequence, newest)) {
            return false;
        }

        newest = sync.sequence;

        // Keep the authoritative copy current. The replica is what a late
        // joiner constructs from and what the interest grid measures
        // distance against, so it must not go stale just because motion
        // moved to its own packet.
        if (sync.Has(Auth::DriverField::kPosition)) {
            vehicle->position = sync.position;
        }

        if (sync.Has(Auth::DriverField::kRotation)) {
            vehicle->rotation = sync.rotation;
        }

        if (sync.Has(Auth::DriverField::kVelocity)) {
            vehicle->velocity = sync.velocity;
        }

        if (sync.Has(Auth::DriverField::kTurnSpeed)) {
            vehicle->angularVelocity = sync.turnSpeed;
        }

        if (sync.Has(Auth::DriverField::kHealth)) {
            vehicle->health = sync.health;
            vehicle->LatchBlown();
        }

        if (sync.Has(Auth::DriverField::kControls)) {
            vehicle->steer       = sync.steer;
            vehicle->accelerator = sync.accelerator;
            vehicle->brake       = sync.brake;
            vehicle->gasPedal    = sync.gasPedal;
            vehicle->brakePedal  = sync.brakePedal;
        }

        if (sync.Has(Auth::DriverField::kFlags)) {
            namespace Flag = Shared::RPC::Vehicle::VehicleFlag;

            vehicle->onGround  = (sync.flags & Flag::kOnGround) != 0;
            vehicle->engineOn  = (sync.flags & Flag::kEngineOn) != 0;
            vehicle->sirenOn   = (sync.flags & Flag::kSirenOn) != 0;
            vehicle->handbrake = (sync.flags & Flag::kHandbrake) != 0;
            vehicle->lightsOn  = (sync.flags & Flag::kLightsOn) != 0;
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return true;
        }

        // Relay verbatim, minus the sender. Unreliable: a stale snapshot is
        // worth less than the newer one already behind it.
        auto relay = sync;

        net->BroadcastRPC(relay, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable, MafiaNet::ToGuid(sender.ownerGUID));

        MafiaNet::BitStream sizing;

        relay.Serialize(&sizing, true);
        Shared::RPC::Metrics::Counters::Get().Record(Shared::RPC::Metrics::PacketType::DriverSync, sizing.GetNumberOfBytesUsed(),
                                                    static_cast<std::uint32_t>(_players.Count() > 0 ? _players.Count() - 1 : 0));

        return true;
    }

    void VehicleManager::UpdateUnoccupiedSyncers() {
        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        // Beyond this nobody needs a car simulated: it is out of everyone's
        // streaming range, so there is nothing to see and nothing to sync.
        // A vehicle with no syncer costs ZERO bandwidth, which is the point.
        constexpr float kSyncerRange = 180.0f;

        for (const auto &[networkId, vehicle] : _vehicles) {
            if (!vehicle) {
                continue;
            }

            // A vehicle with an owner is that owner's business - they are
            // driving it, or they drove it and it is still rolling under
            // their simulation. Electing a second authority for it is the
            // double-owner bug.
            if (vehicle->ownerGUID != MafiaNet::UNASSIGNED_PEER_GUID) {
                if (const auto stale = _syncers.find(networkId); stale != _syncers.end()) {
                    Shared::RPC::VehicleSyncOwner stop;

                    stop.FromParameters(networkId, false, 0, _wireIds.Find(networkId).value);
                    net->SendRPC(stop, MafiaNet::ToGuid(stale->second), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
                    _syncers.erase(stale);
                }

                continue;
            }

            // Nearest player wins. Nearest rather than first-found because
            // the closest client has the best view of the collisions the
            // vehicle is involved in, and is the least likely to stream it
            // out mid-simulation.
            MafiaNet::PeerGuid best     = MafiaNet::UNASSIGNED_PEER_GUID;
            float              bestDist = kSyncerRange * kSyncerRange;

            _players.ForEach([&](Shared::PlayerEntity *player) {
                if (!Shared::SameDimension(player->dimension, vehicle->dimension)) {
                    return;
                }

                const glm::vec3 delta = player->position - vehicle->position;
                const float     distSq = glm::dot(delta, delta);

                if (distSq < bestDist) {
                    bestDist = distSq;
                    best     = player->ownerGUID;
                }
            });

            const auto current = _syncers.find(networkId);
            const auto held    = current == _syncers.end() ? MafiaNet::UNASSIGNED_PEER_GUID : current->second;

            if (held == best) {
                continue;
            }

            // STOP BEFORE START, AND BOTH RELIABLE ORDERED. If the start
            // reached the new syncer before the stop reached the old one,
            // two clients would simulate the same physical car and fight
            // over it. Ordering on the Events channel is what makes this a
            // handoff rather than a race.
            if (held != MafiaNet::UNASSIGNED_PEER_GUID) {
                Shared::RPC::VehicleSyncOwner stop;

                stop.FromParameters(networkId, false, 0, _wireIds.Find(networkId).value);
                net->SendRPC(stop, MafiaNet::ToGuid(held), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
                Shared::RPC::Metrics::Counters::Get().Record(Shared::RPC::Metrics::PacketType::SyncOwner, 11);
            }

            if (best == MafiaNet::UNASSIGNED_PEER_GUID) {
                _syncers.erase(networkId);
                continue;
            }

            Shared::RPC::VehicleSyncOwner start;

            start.FromParameters(networkId, true, 0, _wireIds.Acquire(networkId).value);
            net->SendRPC(start, MafiaNet::ToGuid(best), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            Shared::RPC::Metrics::Counters::Get().Record(Shared::RPC::Metrics::PacketType::SyncOwner, 11);

            _syncers[networkId] = best;
        }
    }

    void VehicleManager::HandleUnoccupiedSync(Shared::PlayerEntity &sender, const Shared::RPC::VehicleUnoccupiedSync &batch) {
        namespace Auth = Shared::RPC::Vehicle;

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        Shared::RPC::VehicleUnoccupiedSync relay;

        relay.sequence = batch.sequence;
        relay.entries.reserve(batch.entries.size());

        // VALIDATED PER ENTRY, NOT PER BATCH. One forged entry must not
        // discard twenty good ones, and twenty good ones must not launder one
        // forged entry through to every client.
        for (const auto &entry : batch.entries) {
            const auto networkId = _wireIds.Resolve(entry.vehicleId);

            if (networkId == 0) {
                continue;
            }

            auto *vehicle = Find(networkId);

            if (!vehicle) {
                continue;
            }

            // Only the assigned syncer, and only while the vehicle really is
            // unowned. A client that was the syncer a moment ago must not
            // keep steering a car somebody has since got into.
            const auto assigned = _syncers.find(networkId);

            if (assigned == _syncers.end() || assigned->second != sender.ownerGUID) {
                continue;
            }

            if (vehicle->ownerGUID != MafiaNet::UNASSIGNED_PEER_GUID) {
                continue;
            }

            if (Auth::ValidateUnoccupiedEntry(entry) != Auth::Reject::None) {
                continue;
            }

            if (entry.Has(Auth::UnoccupiedField::kPosition)) {
                vehicle->position = entry.position;
            }

            if (entry.Has(Auth::UnoccupiedField::kRotation)) {
                vehicle->rotation = entry.rotation;
            }

            if (entry.Has(Auth::UnoccupiedField::kVelocity)) {
                vehicle->velocity = entry.velocity;
            }

            if (entry.Has(Auth::UnoccupiedField::kTurnSpeed)) {
                vehicle->angularVelocity = entry.turnSpeed;
            }

            if (entry.Has(Auth::UnoccupiedField::kHealth)) {
                vehicle->health = entry.health;
                vehicle->LatchBlown();
            }

            relay.entries.push_back(entry);
        }

        if (relay.entries.empty()) {
            return;
        }

        net->BroadcastRPC(relay, MafiaNet::Priority::Medium, MafiaNet::Reliability::Unreliable, MafiaNet::ToGuid(sender.ownerGUID));

        MafiaNet::BitStream sizing;

        relay.Serialize(&sizing, true);
        Shared::RPC::Metrics::Counters::Get().Record(Shared::RPC::Metrics::PacketType::UnoccupiedSync, sizing.GetNumberOfBytesUsed(),
                                                    static_cast<std::uint32_t>(_players.Count() > 0 ? _players.Count() - 1 : 0));
    }

    void VehicleManager::SetInterior(Shared::VehicleEntity &vehicle, std::uint8_t interior) {
        vehicle.interior = interior;

        if (static_cast<std::uint64_t>(vehicle.ownerGUID) == 0 || !_players.Find(vehicle.ownerGUID)) {
            return;
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        Shared::RPC::WorldSpace message;
        message.FromParameters(static_cast<std::uint64_t>(vehicle.GetNetworkID()), interior, vehicle.dimension, Shared::kWorldSpaceInterior);
        net->SendRPC(message, MafiaNet::ToGuid(vehicle.ownerGUID));
    }

    void VehicleManager::SetDimension(Shared::VehicleEntity &vehicle, std::uint16_t dimension) {
        vehicle.dimension = dimension;

        if (static_cast<std::uint64_t>(vehicle.ownerGUID) == 0 || !_players.Find(vehicle.ownerGUID)) {
            return;
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        Shared::RPC::WorldSpace message;
        message.FromParameters(static_cast<std::uint64_t>(vehicle.GetNetworkID()), vehicle.interior, dimension, Shared::kWorldSpaceDimension);
        net->SendRPC(message, MafiaNet::ToGuid(vehicle.ownerGUID));
    }

    void VehicleManager::SetColors(Shared::VehicleEntity &vehicle, std::uint32_t c1, std::uint32_t c2, std::uint32_t c3, std::uint32_t c4) {
        vehicle.SetColours(c1, c2, c3, c4);
    }

    void VehicleManager::SetPaintjob(Shared::VehicleEntity &vehicle, std::uint8_t paintjob) {
        if (!Shared::IsPaintjob(paintjob)) {
            return;
        }

        vehicle.paintjob = paintjob;
    }

    bool VehicleManager::AddUpgrade(Shared::VehicleEntity &vehicle, std::uint16_t upgradeId) {
        if (!Shared::ModelCatalog::IsUpgrade(upgradeId)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Refusing vehicle upgrade {}; not in the catalog", upgradeId);
            return false;
        }

        return vehicle.SetUpgrade(upgradeId);
    }

    bool VehicleManager::RemoveUpgrade(Shared::VehicleEntity &vehicle, std::uint16_t upgradeId) {
        const int slot = Shared::UpgradeSlot(upgradeId);

        if (slot < 0) {
            return false;
        }

        if (vehicle.upgrades[static_cast<std::size_t>(slot)] != upgradeId) {
            return false;
        }

        vehicle.ClearUpgradeSlot(slot);
        return true;
    }

    void VehicleManager::RemoveAllUpgrades(Shared::VehicleEntity &vehicle) {
        vehicle.upgrades = {};
    }

    void VehicleManager::Fix(Shared::VehicleEntity &vehicle) {
        vehicle.health           = 1000.0f;
        vehicle.blown            = false;
        vehicle.damageEngine     = 0;
        vehicle.doorsPacked      = 0;
        vehicle.wheelsPacked     = 0;
        vehicle.panels           = 0;
        vehicle.lights           = 0;
        vehicle.doorRatiosPacked = 0;
        vehicle.ForceState();
    }

    void VehicleManager::Blow(Shared::VehicleEntity &vehicle) {
        vehicle.LatchBlown();
        vehicle.ForceState();
    }

    Shared::VehicleEntity *VehicleManager::Find(std::uint64_t networkId) const {
        const auto found = _vehicles.find(networkId);

        return found == _vehicles.end() ? nullptr : found->second;
    }
} // namespace VantaMP::Game
