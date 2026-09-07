#include "vehicle_manager.h"

#include "game/player_manager.h"
#include "shared/entity/player_entity.h"
#include "shared/rpc/player_enter_vehicle.h"
#include "shared/rpc/player_exit_vehicle.h"
#include "shared/rpc/vehicle_damage_report.h"
#include "shared/catalog/model_catalog.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/vehicle/upgrades.h"
#include "shared/rpc/world_space.h"
#include "shared/world/world_space.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>

namespace VantaMP::Game {
    VehicleManager::VehicleManager(PlayerManager &players) : _players(players) {}

    std::uint64_t VehicleManager::Create(std::uint32_t modelId, float x, float y, float z, float heading, std::uint8_t primaryColour, std::uint8_t secondaryColour,
                                         std::uint8_t interior, std::uint16_t dimension) {
        if (!Shared::ModelCatalog::IsVehicle(modelId)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Refusing vehicle model {} — not in the catalog", modelId);
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
        vehicle->rotation        = glm::quat(glm::vec3(0.0f, 0.0f, heading));
        vehicle->health          = 1000.0f;
        vehicle->engineOn        = true;
        vehicle->blown           = false;
        vehicle->interior        = interior;
        vehicle->dimension       = dimension;

        const auto id = static_cast<std::uint64_t>(vehicle->GetNetworkID());

        _vehicles[id] = vehicle;

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

        if (assigned == 0) {
            vehicle->SetOwner(player.ownerGUID);
        }

        Shared::RPC::PlayerEnterVehicle enter;
        enter.FromParameters(playerId, vehicleId, assigned, warp, door);
        net->BroadcastRPC(enter, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Player {} entered vehicle {} seat {} (asked {})", playerId, vehicleId, assigned, seat);
    }

    void VehicleManager::HandleExit(Shared::PlayerEntity &player, bool warp, std::uint8_t door) {
        const auto playerId = static_cast<std::uint64_t>(player.GetNetworkID());

        Vacate(player);

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        // Broadcast even when they were not seated. A jack walk never
        // took the seat; remotes still have to abort the enter task.
        Shared::RPC::PlayerExitVehicle exit;
        exit.FromParameters(playerId, warp, door);
        net->BroadcastRPC(exit, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void VehicleManager::HandleDisconnect(MafiaNet::PeerGuid guid) {
        auto *player = _players.Find(guid);

        if (!player) {
            return;
        }

        if (player->vehicleNetworkId != 0) {
            HandleExit(*player, true, 0xFF);
        }

        for (auto &[id, vehicle] : _vehicles) {
            if (vehicle && vehicle->ownerGUID == guid) {
                vehicle->SetOwner(MafiaNet::UNASSIGNED_PEER_GUID);
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Vehicle {} returned to the server; its owner disconnected", id);
            }
        }
    }

    void VehicleManager::HandleDamage(std::uint64_t vehicleId, std::uint8_t engine, std::uint64_t doors, std::uint32_t wheels, std::uint32_t panels, std::uint32_t lights,
                                      std::uint64_t doorRatios, bool applyRatios, float health) {
        auto *vehicle = Find(vehicleId);

        if (!vehicle) {
            return;
        }

        vehicle->damageEngine = engine > vehicle->damageEngine ? engine : vehicle->damageEngine;
        vehicle->doorsPacked  = Shared::VehicleEntity::WorseBytes(vehicle->doorsPacked, doors, Shared::VehicleEntity::kDoorCount);
        vehicle->wheelsPacked = static_cast<std::uint32_t>(Shared::VehicleEntity::WorseBytes(vehicle->wheelsPacked, wheels, Shared::VehicleEntity::kWheelCount));
        vehicle->panels       = Shared::VehicleEntity::WorsePacked(vehicle->panels, panels, Shared::VehicleEntity::kPanelCount, 4);
        vehicle->lights       = Shared::VehicleEntity::WorsePacked(vehicle->lights, lights, Shared::VehicleEntity::kLightCount, 2);
        vehicle->health       = health < vehicle->health ? health : vehicle->health;
        vehicle->LatchBlown();

        // REPLACE, DO NOT MAX - and only when nobody is driving.
        // A passenger report used to stomp the owner's close, then the
        // owner pushed shut, then the passenger opened it again: that
        // is the door slamming on a 100ms beat.
        bool hasDriver = false;

        _players.ForEach([&](Shared::PlayerEntity *player) {
            if (player->vehicleNetworkId == vehicleId && player->vehicleSeat == 0) {
                hasDriver = true;
            }
        });

        if (applyRatios && !hasDriver) {
            vehicle->doorRatiosPacked = doorRatios;
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        Shared::RPC::VehicleDamageReport report;
        report.FromParameters(vehicleId, vehicle->damageEngine, vehicle->doorsPacked, vehicle->wheelsPacked, vehicle->panels, vehicle->lights, vehicle->doorRatiosPacked,
                              applyRatios && !hasDriver, vehicle->health);
        net->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
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
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Refusing vehicle upgrade {} — not in the catalog", upgradeId);
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

    Shared::VehicleEntity *VehicleManager::Find(std::uint64_t networkId) const {
        const auto found = _vehicles.find(networkId);

        return found == _vehicles.end() ? nullptr : found->second;
    }
} // namespace VantaMP::Game
