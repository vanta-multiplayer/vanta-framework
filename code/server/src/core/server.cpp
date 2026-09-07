#include "server.h"

#include "scripting/bindings.h"
#include "shared/player_entity.h"
#include "shared/vehicle_entity.h"
#include "shared/rpc/player_death.h"
#include "shared/rpc/player_enter_vehicle.h"
#include "shared/rpc/player_exit_vehicle.h"
#include "shared/rpc/vehicle_damage_report.h"
#include "shared/rpc/player_reload.h"
#include "shared/rpc/player_shot.h"
#include "shared/spawn_points.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/entity_registry.h>
#include <voice/server/voice_server.h>

namespace VantaMP {
    namespace Replication = Framework::Networking::Replication;

    void Server::PostInit() {
        // The type has to exist before any entity of it can be created, and its
        // id - the CRC32 of this name - is what goes on the wire. The client
        // registers the same name, so both sides agree without either shipping
        // a number.
        Replication::EntityRegistry::Get().Register<Shared::PlayerEntity>(Shared::PlayerEntity::kTypeName);
        Replication::EntityRegistry::Get().Register<Shared::VehicleEntity>(Shared::VehicleEntity::kTypeName);

        if (auto *voice = Framework::CoreModules::GetVoiceServer()) {
            voice->SetProximityRange(40.0f);
        }

        // Combat events: the client that fired cannot name itself. The GUID
        // on the packet is the connection; the entity id is what remotes
        // resolve. Relayed to everyone except the shooter so they do not
        // play their own shot twice.
        if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
            net->RegisterRPC<Shared::RPC::PlayerShot>([this, net](const Shared::RPC::PlayerShot &incoming, MafiaNet::Packet *packet) {
                auto *player = _players.Find(MafiaNet::ToPeerGuid(packet->guid));

                if (!player) {
                    return;
                }

                Shared::RPC::PlayerShot shot = incoming;
                shot.networkId               = static_cast<std::uint64_t>(player->GetNetworkID());
                net->BroadcastRPC(shot, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, packet->guid);
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Relayed shot from {}", shot.networkId);
            });

            net->RegisterRPC<Shared::RPC::PlayerReload>([this, net](const Shared::RPC::PlayerReload &incoming, MafiaNet::Packet *packet) {
                auto *player = _players.Find(MafiaNet::ToPeerGuid(packet->guid));

                if (!player) {
                    return;
                }

                Shared::RPC::PlayerReload reload = incoming;
                reload.networkId                 = static_cast<std::uint64_t>(player->GetNetworkID());
                net->BroadcastRPC(reload, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, packet->guid);
            });

            net->RegisterRPC<Shared::RPC::PlayerDeath>([this](const Shared::RPC::PlayerDeath &incoming, MafiaNet::Packet *packet) {
                auto *player = _players.Find(MafiaNet::ToPeerGuid(packet->guid));

                if (!player) {
                    return;
                }

                const auto id = static_cast<std::uint64_t>(player->GetNetworkID());

                player->health = 0.0f;
                player->armour = 0.0f;

                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player {} died (killer {} weapon {})", id, incoming.killerId, incoming.weaponType);

                if (Scripting::EmitPlayerDeath(id, incoming.killerId, incoming.weaponType)) {
                    return;
                }

                _players.Spawn(MafiaNet::ToPeerGuid(packet->guid), Shared::Spawn::kDefaultX, Shared::Spawn::kDefaultY, Shared::Spawn::kDefaultZ);
            });

            net->RegisterRPC<Shared::RPC::PlayerEnterVehicle>([this](const Shared::RPC::PlayerEnterVehicle &incoming, MafiaNet::Packet *packet) {
                auto *player = _players.Find(MafiaNet::ToPeerGuid(packet->guid));

                if (!player) {
                    return;
                }

                _vehicles.HandleEnter(*player, incoming.vehicleId, incoming.seat, incoming.warp, incoming.door);
            });

            net->RegisterRPC<Shared::RPC::PlayerExitVehicle>([this](const Shared::RPC::PlayerExitVehicle &incoming, MafiaNet::Packet *packet) {
                auto *player = _players.Find(MafiaNet::ToPeerGuid(packet->guid));

                if (!player) {
                    return;
                }

                _vehicles.HandleExit(*player, incoming.warp, incoming.door);
            });

            net->RegisterRPC<Shared::RPC::VehicleDamageReport>([this](const Shared::RPC::VehicleDamageReport &incoming, MafiaNet::Packet *packet) {
                if (!_players.Find(MafiaNet::ToPeerGuid(packet->guid))) {
                    return;
                }

                _vehicles.HandleDamage(incoming.vehicleId, incoming.damageEngine, incoming.doorsPacked, incoming.wheelsPacked, incoming.panels, incoming.lights,
                                       incoming.doorRatiosPacked, incoming.applyRatios, incoming.health);
            });
        }

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("VantaMP server ready");
    }

    void Server::PostUpdate() {}

    void Server::PreShutdown() {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("VantaMP server shutting down");
    }

    void Server::ModuleRegister(Framework::Scripting::Engine *engine) {
        Scripting::Register(engine, _players, _vehicles);
    }

    void Server::OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &data) {
        _players.Add(data.guid, data.nickname);
    }

    void Server::OnPlayerReady(MafiaNet::PeerGuid guid) {
        // CONNECTING IS NOT SPAWNING. Until this the player has an entity but no
        // place in the world. A resource listening for playerReady decides
        // whether they appear, and where. The C++ spawn below is only the
        // fallback for a server with no resource loaded.
        auto *player = _players.Find(guid);

        if (player && Scripting::EmitPlayerReady(static_cast<std::uint64_t>(player->GetNetworkID()))) {
            return;
        }

        _players.Spawn(guid, Shared::Spawn::kDefaultX, Shared::Spawn::kDefaultY, Shared::Spawn::kDefaultZ);
    }

    void Server::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        _vehicles.HandleDisconnect(guid);
        _players.Remove(guid);
    }
} // namespace VantaMP
