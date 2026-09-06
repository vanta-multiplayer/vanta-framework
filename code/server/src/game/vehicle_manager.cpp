#include "vehicle_manager.h"

#include "game/player_manager.h"
#include "shared/player_entity.h"
#include "shared/rpc/player_enter_vehicle.h"
#include "shared/rpc/player_exit_vehicle.h"
#include "shared/rpc/vehicle_damage_report.h"
#include "shared/vehicle_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>

namespace VantaMP::Game {
    VehicleManager::VehicleManager(PlayerManager &players) : _players(players) {}

    std::uint64_t VehicleManager::Create(std::uint32_t modelId, float x, float y, float z, float heading, std::uint8_t primaryColour, std::uint8_t secondaryColour) {
        if (modelId < Shared::VehicleEntity::kFirstModel || modelId > Shared::VehicleEntity::kLastModel) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Refusing vehicle model {}", modelId);
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
        vehicle->position        = glm::vec3(x, y, z);
        vehicle->rotation        = glm::quat(glm::vec3(0.0f, 0.0f, heading));
        vehicle->health          = 1000.0f;
        vehicle->engineOn        = true;

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

    namespace {
        std::uint64_t WorseBytes(std::uint64_t left, std::uint64_t right, int count) {
            std::uint64_t worse = 0;

            for (int i = 0; i < count; ++i) {
                const auto shift = static_cast<std::uint64_t>(i * 8);
                const auto a     = static_cast<std::uint8_t>((left >> shift) & 0xFFu);
                const auto b     = static_cast<std::uint8_t>((right >> shift) & 0xFFu);

                worse |= static_cast<std::uint64_t>(a > b ? a : b) << shift;
            }

            return worse;
        }
    } // namespace

    void VehicleManager::HandleDamage(std::uint64_t vehicleId, std::uint8_t engine, std::uint64_t doors, std::uint32_t wheels, std::uint32_t panels, std::uint32_t lights,
                                      std::uint64_t doorRatios, bool applyRatios) {
        auto *vehicle = Find(vehicleId);

        if (!vehicle) {
            return;
        }

        vehicle->damageEngine = engine > vehicle->damageEngine ? engine : vehicle->damageEngine;
        vehicle->doorsPacked  = WorseBytes(vehicle->doorsPacked, doors, Shared::VehicleEntity::kDoorCount);
        vehicle->wheelsPacked = static_cast<std::uint32_t>(WorseBytes(vehicle->wheelsPacked, wheels, Shared::VehicleEntity::kWheelCount));
        vehicle->panels       = panels > vehicle->panels ? panels : vehicle->panels;
        vehicle->lights       = lights > vehicle->lights ? lights : vehicle->lights;

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
                              applyRatios && !hasDriver);
        net->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    Shared::VehicleEntity *VehicleManager::Find(std::uint64_t networkId) const {
        const auto found = _vehicles.find(networkId);

        return found == _vehicles.end() ? nullptr : found->second;
    }
} // namespace VantaMP::Game
