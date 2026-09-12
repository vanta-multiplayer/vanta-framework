#include "player_manager.h"

#include "shared/catalog/model_catalog.h"
#include "shared/entity/player_entity.h"
#include "shared/rpc/common/metrics.h"
#include "shared/rpc/player/spawn.h"
#include "shared/rpc/player/onfoot_sync.h"
#include "shared/rpc/vehicle/sync_owner.h"
#include "shared/vehicle/validate.h"
#include "shared/rpc/player/script.h"
#include "shared/rpc/player/weapon.h"
#include "shared/rpc/world_space.h"
#include "shared/world/world_space.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <world/types/player.h>

namespace VantaMP::Game {
    bool PlayerManager::Add(MafiaNet::PeerGuid guid, const std::string &nickname) {
        const auto logger = Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER);

        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication) {
            return false;
        }

        auto *player = replication->CreateEntity<Shared::PlayerEntity>();

        if (!player) {
            logger->error("Could not create an entity for {}; they will connect to nothing", nickname);
            return false;
        }

        player->nickname = nickname;

        Framework::World::Archetypes::PlayerArchetype archetype;
        archetype.SetupServer(player, guid);

        replication->SetViewer(guid, player);

        _players[guid] = player;

        const std::string name = nickname.empty() ? "unknown" : nickname;

        logger->info("Player {} connected as network id {}", name, static_cast<std::uint64_t>(player->GetNetworkID()));

        return true;
    }

    void PlayerManager::Remove(MafiaNet::PeerGuid guid) {
        const auto found = _players.find(guid);

        if (found == _players.end()) {
            // A peer that never got as far as an entity - a failed handshake, or
            // a disconnect during connect. Nothing to tear down.
            return;
        }

        // Destroying the entity is what tells everyone else this player is gone;
        // there is no separate despawn message.
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->DestroyEntity(found->second);
        }

        _players.erase(found);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player {} disconnected", found->second->nickname.empty() ? "unknown" : found->second->nickname);
    }

    bool PlayerManager::Spawn(MafiaNet::PeerGuid guid, float x, float y, float z, float heading, std::uint32_t modelId, float health, float armour,
                             std::uint8_t interior, std::uint16_t dimension) {
        auto *player = Find(guid);

        if (!player) {
            return false;
        }

        // The server's own copy first, because that is what the interest grid
        // reads. Sending the client a position it then reports back would work
        // eventually, but leaves a window where nobody can see anybody.
        player->position  = glm::vec3(x, y, z);
        player->health    = health;
        player->armour    = armour;
        player->interior  = interior;
        player->dimension = dimension;

        if (modelId != 0) {
            if (Shared::ModelCatalog::IsPed(modelId)) {
                player->modelId = modelId;
            }
            else {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Refusing ped model {}; not in the catalog", modelId);
            }
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return false;
        }

        Shared::RPC::LocalPlayerSpawn message;
        message.FromParameters(x, y, z, heading, player->modelId, health, armour, interior, dimension);

        net->SendRPC(message, MafiaNet::ToGuid(guid));

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Spawned player {} at {:.1f}, {:.1f}, {:.1f}", player->nickname.empty() ? "unknown" : player->nickname, x, y, z);

        return true;
    }

    namespace {
        void SendWorldSpace(MafiaNet::PeerGuid guid, std::uint64_t networkId, std::uint8_t interior, std::uint16_t dimension, std::uint8_t flags) {
            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net || static_cast<std::uint64_t>(guid) == 0) {
                return;
            }

            Shared::RPC::WorldSpace message;
            message.FromParameters(networkId, interior, dimension, flags);
            net->SendRPC(message, MafiaNet::ToGuid(guid));
        }
    } // namespace

    void PlayerManager::SetInterior(Shared::PlayerEntity &player, std::uint8_t interior) {
        player.interior = interior;
        SendWorldSpace(player.ownerGUID, static_cast<std::uint64_t>(player.GetNetworkID()), interior, player.dimension, Shared::kWorldSpaceInterior);
    }

    void PlayerManager::SetDimension(Shared::PlayerEntity &player, std::uint16_t dimension) {
        player.dimension = dimension;
        SendWorldSpace(player.ownerGUID, static_cast<std::uint64_t>(player.GetNetworkID()), player.interior, dimension, Shared::kWorldSpaceDimension);
    }

    void PlayerManager::SendScript(Shared::PlayerEntity &player, std::uint8_t flags, std::uint8_t flags2) {
        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net || static_cast<std::uint64_t>(player.ownerGUID) == 0 || (flags == 0 && flags2 == 0)) {
            return;
        }

        Shared::RPC::PlayerScript message;
        message.flags         = flags;
        message.health        = player.health;
        message.armour        = player.armour;
        message.modelId       = player.modelId;
        message.money         = player.money;
        message.wantedLevel   = player.wantedLevel;
        message.frozen        = player.frozen;
        message.flags2        = flags2;
        message.collisions    = player.collisions;
        message.fightingStyle = player.fightingStyle;
        message.oxygen        = player.oxygen;
        message.walkingStyle  = player.walkingStyle;
        message.alpha         = player.alpha;
        message.onFire        = player.onFire;
        message.maxHealth     = player.maxHealth;
        message.hasJetPack    = player.hasJetPack;
        net->SendRPC(message, MafiaNet::ToGuid(player.ownerGUID));
    }

    void PlayerManager::SendWeapon(Shared::PlayerEntity &player, std::uint8_t action, std::uint32_t weaponType, std::uint32_t ammo) {
        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net || static_cast<std::uint64_t>(player.ownerGUID) == 0) {
            return;
        }

        Shared::RPC::PlayerWeapon message;
        message.FromParameters(action, weaponType, ammo);
        net->SendRPC(message, MafiaNet::ToGuid(player.ownerGUID));
    }

    Shared::PlayerEntity *PlayerManager::Find(MafiaNet::PeerGuid guid) const {
        const auto found = _players.find(guid);

        return found == _players.end() ? nullptr : found->second;
    }

    void PlayerManager::AnnounceWireId(std::uint64_t networkId, MafiaNet::PeerGuid to) {
        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        const auto id = _wireIds.Acquire(networkId);

        if (!id.Valid()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Wire id space exhausted; player {} stays on plain replication", networkId);
            return;
        }

        // Reuses the vehicle handoff packet rather than adding a near-identical
        // player one: it already carries exactly "this entity has this wire id"
        // and nothing in it is vehicle-specific. A second packet would be a
        // second Huffman identifier to pay for the same fact.
        Shared::RPC::VehicleSyncOwner message;

        message.FromParameters(networkId, false, 0, id.value);

        if (to == MafiaNet::UNASSIGNED_PEER_GUID) {
            net->BroadcastRPC(message, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
        else {
            net->SendRPC(message, MafiaNet::ToGuid(to), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    void PlayerManager::AnnounceAllWireIds(MafiaNet::PeerGuid to) {
        ForEach([&](Shared::PlayerEntity *player) {
            AnnounceWireId(static_cast<std::uint64_t>(player->GetNetworkID()), to);
        });
    }

    void PlayerManager::HandleOnFootSync(Shared::PlayerEntity &sender, const Shared::RPC::PlayerOnFootSync &sync) {
        namespace Field = Shared::RPC::Player::OnFootField;

        const auto networkId = _wireIds.Resolve(sync.playerId);

        // A PLAYER MAY ONLY MOVE THEMSELVES. The wire id names whose movement
        // this is; if it is not the sender's own, the packet is forged. This
        // is the player-side equivalent of the vehicle driver-authority check
        // and must stay just as strict.
        if (networkId == 0 || networkId != static_cast<std::uint64_t>(sender.GetNetworkID())) {
            return;
        }

        // A SEATED PLAYER HAS NO ON-FOOT MOTION. Their transform belongs to
        // the vehicle, and accepting this would let a passenger move their ped
        // out of the car they are sitting in.
        if (sender.vehicleNetworkId != 0) {
            return;
        }

        // Only the fields the mask claims are validated: an absent field is
        // not invalid, it is simply not being set.
        if (sync.Has(Field::kPosition) && !Shared::IsValidPosition(sync.position)) {
            return;
        }

        if (sync.Has(Field::kVelocity) && !Shared::IsValidVelocity(sync.velocity)) {
            return;
        }

        if (sync.Has(Field::kHeading) && !Shared::IsFiniteFloat(sync.heading)) {
            return;
        }

        if (sync.Has(Field::kInput) && !Shared::IsFiniteFloat(sync.cameraRotation)) {
            return;
        }

        auto &seen = _sequences[networkId];

        if (seen.haveOnFoot && !Shared::RPC::IsNewerSequence(sync.sequence, seen.onFoot)) {
            return;
        }

        seen.onFoot     = sync.sequence;
        seen.haveOnFoot = true;

        // Keep the authoritative replica current: it is what a late joiner
        // constructs from and what the interest grid measures distance
        // against, so it must not go stale because motion moved to a packet.
        if (sync.Has(Field::kPosition)) {
            sender.position = sync.position;
        }

        if (sync.Has(Field::kVelocity)) {
            sender.velocity = sync.velocity;
        }

        if (sync.Has(Field::kInput)) {
            sender.leftStickX     = sync.leftStickX;
            sender.leftStickY     = sync.leftStickY;
            sender.accelerator    = sync.accelerator;
            sender.brake          = sync.brake;
            sender.buttons        = sync.buttons;
            sender.cameraRotation = sync.cameraRotation;
        }

        if (sync.Has(Field::kAim)) {
            if (!Shared::IsFiniteFloat(sync.aimHorizontal) || !Shared::IsFiniteFloat(sync.aimVertical)) {
                return;
            }

            sender.aimHorizontal = sync.aimHorizontal;
            sender.aimVertical   = sync.aimVertical;
        }

        if (sync.Has(Field::kFlags)) {
            namespace Flag = Shared::RPC::Player::PedFlag;

            sender.isDucked  = (sync.flags & Flag::kDucked) != 0;
            sender.isInAir   = (sync.flags & Flag::kInAir) != 0;
            sender.isInWater = (sync.flags & Flag::kInWater) != 0;
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        auto relay = sync;

        net->BroadcastRPC(relay, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable, MafiaNet::ToGuid(sender.ownerGUID));

        MafiaNet::BitStream sizing;

        relay.Serialize(&sizing, true);
        Shared::RPC::Metrics::Counters::Get().Record(Shared::RPC::Metrics::PacketType::PlayerOnFootSync, sizing.GetNumberOfBytesUsed(),
                                                     static_cast<std::uint32_t>(Count() > 0 ? Count() - 1 : 0));
    }

} // namespace VantaMP::Game
