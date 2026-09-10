#include "net.h"

#include "core/server.h"
#include "eac/eac.h"
#include "scripting/bindings.h"
#include "shared/entity/player_entity.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/rpc/eac_messages.h"
#include "shared/rpc/player_death.h"
#include "shared/rpc/player_enter_vehicle.h"
#include "shared/rpc/player_exit_vehicle.h"
#include "shared/rpc/player_reload.h"
#include "shared/rpc/player_shot.h"
#include "shared/rpc/vehicle_damage_report.h"
#include "shared/world/spawn_points.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/entity_registry.h>

namespace VantaMP::Net {
    namespace Replication = Framework::Networking::Replication;

    void Register(Server &server) {
        // The type has to exist before any entity of it can be created, and its
        // id - the CRC32 of this name - is what goes on the wire. The client
        // registers the same name, so both sides agree without either shipping
        // a number.
        Replication::EntityRegistry::Get().Register<Shared::PlayerEntity>(Shared::PlayerEntity::kTypeName);
        Replication::EntityRegistry::Get().Register<Shared::VehicleEntity>(Shared::VehicleEntity::kTypeName);

        // Combat events: the client that fired cannot name itself. The GUID
        // on the packet is the connection; the entity id is what remotes
        // resolve. Relayed to everyone except the shooter so they do not
        // play their own shot twice.
        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        net->RegisterRPC<Shared::RPC::PlayerShot>([&server, net](const Shared::RPC::PlayerShot &incoming, MafiaNet::Packet *packet) {
            auto *player = server.Players().Find(MafiaNet::ToPeerGuid(packet->guid));

            if (!player) {
                return;
            }

            Shared::RPC::PlayerShot shot = incoming;
            shot.networkId               = static_cast<std::uint64_t>(player->GetNetworkID());
            net->BroadcastRPC(shot, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, packet->guid);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Relayed shot from {}", shot.networkId);
        });

        net->RegisterRPC<Shared::RPC::PlayerReload>([&server, net](const Shared::RPC::PlayerReload &incoming, MafiaNet::Packet *packet) {
            auto *player = server.Players().Find(MafiaNet::ToPeerGuid(packet->guid));

            if (!player) {
                return;
            }

            Shared::RPC::PlayerReload reload = incoming;
            reload.networkId                 = static_cast<std::uint64_t>(player->GetNetworkID());
            net->BroadcastRPC(reload, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, packet->guid);
        });

        net->RegisterRPC<Shared::RPC::PlayerDeath>([&server](const Shared::RPC::PlayerDeath &incoming, MafiaNet::Packet *packet) {
            auto *player = server.Players().Find(MafiaNet::ToPeerGuid(packet->guid));

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

            server.Players().Spawn(MafiaNet::ToPeerGuid(packet->guid), Shared::Spawn::kDefaultX, Shared::Spawn::kDefaultY, Shared::Spawn::kDefaultZ);
        });

        net->RegisterRPC<Shared::RPC::PlayerEnterVehicle>([&server](const Shared::RPC::PlayerEnterVehicle &incoming, MafiaNet::Packet *packet) {
            auto *player = server.Players().Find(MafiaNet::ToPeerGuid(packet->guid));

            if (!player) {
                return;
            }

            server.Vehicles().HandleEnter(*player, incoming.vehicleId, incoming.seat, incoming.warp, incoming.door);
        });

        net->RegisterRPC<Shared::RPC::PlayerExitVehicle>([&server](const Shared::RPC::PlayerExitVehicle &incoming, MafiaNet::Packet *packet) {
            auto *player = server.Players().Find(MafiaNet::ToPeerGuid(packet->guid));

            if (!player) {
                return;
            }

            server.Vehicles().HandleExit(*player, incoming.warp, incoming.door, incoming.abortEnter, incoming.doorRatio, incoming.knockedOff);
        });

        net->RegisterRPC<Shared::RPC::VehicleDamageReport>([&server](const Shared::RPC::VehicleDamageReport &incoming, MafiaNet::Packet *packet) {
            if (!server.Players().Find(MafiaNet::ToPeerGuid(packet->guid))) {
                return;
            }

            server.Vehicles().HandleDamage(incoming.vehicleId, incoming.damageEngine, incoming.doorsPacked, incoming.wheelsPacked, incoming.panels,
                                           incoming.lights, incoming.doorRatiosPacked, incoming.applyRatios, incoming.health);
        });

        net->RegisterRPC<Shared::RPC::EacRegister>([&server](const Shared::RPC::EacRegister &incoming, MafiaNet::Packet *packet) {
            Eac::HandleRegister(server, MafiaNet::ToPeerGuid(packet->guid), incoming);
        });

        net->RegisterRPC<Shared::RPC::EacOpaque>([](const Shared::RPC::EacOpaque &incoming, MafiaNet::Packet *packet) {
            Eac::HandleOpaqueFromClient(MafiaNet::ToPeerGuid(packet->guid), incoming);
        });
    }
} // namespace VantaMP::Net
