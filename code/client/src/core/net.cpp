#include "net.h"

#include "game/entities/local_player.h"
#include "game/sync/player_sync.h"
#include "game/sync/shots.h"
#include "game/sync/vehicle_sync.h"
#include "game/sync/world_sync.h"
#include "shared/entity/player_entity.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/rpc/local_player_spawn.h"
#include "shared/rpc/player_enter_vehicle.h"
#include "shared/rpc/player_exit_vehicle.h"
#include "shared/rpc/player_occupancy_abort.h"
#include "shared/rpc/player_script.h"
#include "shared/rpc/player_weapon.h"
#include "shared/rpc/player_reload.h"
#include "shared/rpc/player_shot.h"
#include "shared/rpc/vehicle_damage_report.h"
#include "shared/rpc/world_env.h"
#include "shared/rpc/world_space.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/replication/entity_registry.h>

namespace VantaMP::Core::Net {
    void Register() {
        // The same name the server registers, so the CRC32 that identifies this
        // type on the wire matches without either side shipping a number. A
        // mismatch here does not fail loudly - the entity simply never
        // constructs, and remote players are invisible for no stated reason.
        Framework::Networking::Replication::EntityRegistry::Get().Register<Shared::PlayerEntity>(Shared::PlayerEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<Shared::VehicleEntity>(Shared::VehicleEntity::kTypeName);

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        // The server placing this player in the world. The GTA ped already
        // exists behind the fade; this is what belongs them and lifts it.
        net->RegisterRPC<Shared::RPC::LocalPlayerSpawn>([](const Shared::RPC::LocalPlayerSpawn &spawn, MafiaNet::Packet *) {
            Game::ApplyLocalSpawn(spawn.x, spawn.y, spawn.z, spawn.heading, spawn.health, spawn.armour, spawn.interior, spawn.dimension);
        });

        net->RegisterRPC<Shared::RPC::WorldSpace>([](const Shared::RPC::WorldSpace &space, MafiaNet::Packet *) {
            Game::ApplyWorldSpace(space);
        });

        net->RegisterRPC<Shared::RPC::WorldEnv>([](const Shared::RPC::WorldEnv &env, MafiaNet::Packet *) {
            Game::ApplyWorldEnv(env);
        });

        net->RegisterRPC<Shared::RPC::PlayerScript>([](const Shared::RPC::PlayerScript &script, MafiaNet::Packet *) {
            Game::ApplyPlayerScript(script);
        });

        net->RegisterRPC<Shared::RPC::PlayerWeapon>([](const Shared::RPC::PlayerWeapon &weapon, MafiaNet::Packet *) {
            Game::ApplyPlayerWeapon(weapon.action, weapon.weaponType, weapon.ammo);
        });

        // Combat events, not snapshot fields. The server stamps the shooter
        // and fans the packet out; we only apply what arrives.
        net->RegisterRPC<Shared::RPC::PlayerShot>([](const Shared::RPC::PlayerShot &shot, MafiaNet::Packet *) {
            Game::ApplyRemoteShot(shot.networkId, shot.weaponType, shot.originX, shot.originY, shot.originZ, shot.targetX, shot.targetY, shot.targetZ,
                                  shot.order, shot.damagedNetworkId, shot.damage);
        });

        net->RegisterRPC<Shared::RPC::PlayerReload>([](const Shared::RPC::PlayerReload &reload, MafiaNet::Packet *) {
            Game::ApplyRemoteReload(reload.networkId);
        });

        net->RegisterRPC<Shared::RPC::PlayerEnterVehicle>([](const Shared::RPC::PlayerEnterVehicle &enter, MafiaNet::Packet *) {
            Game::ApplyRemoteEnterVehicle(enter.networkId, enter.vehicleId, enter.seat, enter.warp, enter.door);
        });

        net->RegisterRPC<Shared::RPC::PlayerExitVehicle>([](const Shared::RPC::PlayerExitVehicle &exit, MafiaNet::Packet *) {
            Game::ApplyRemoteExitVehicle(exit.networkId, exit.warp, exit.door);
        });

        net->RegisterRPC<Shared::RPC::PlayerOccupancyAbort>([](const Shared::RPC::PlayerOccupancyAbort &abort, MafiaNet::Packet *) {
            Game::ApplyOccupancyAbort(abort.networkId, abort.vehicleId, abort.kind, abort.seat, abort.door);
        });

        net->RegisterRPC<Shared::RPC::VehicleDamageReport>([](const Shared::RPC::VehicleDamageReport &report, MafiaNet::Packet *) {
            Game::ApplyRemoteVehicleDamage(report.vehicleId, report.damageEngine, report.doorsPacked, report.wheelsPacked, report.panels, report.lights,
                                           report.doorRatiosPacked, report.applyRatios, report.health);
        });
    }
} // namespace VantaMP::Core::Net
