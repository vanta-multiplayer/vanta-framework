#include "game/sync/player_sync.h"

#include "game/sync/player/state.h"

#include "game/sync/death.h"
#include "game/sync/vehicle_sync.h"
#include "game/sync/world_sync.h"
#include "game/entities/local_player.h"
#include "sdk/player/slots.h"
#include "game/entities/remote_players.h"
#include "sdk/game.h"
#include "shared/entity/player_entity.h"
#include "shared/entity/state_forced.h"
#include "shared/rpc/common/metrics.h"
#include "shared/rpc/common/wire_registry.h"
#include "shared/rpc/player/onfoot_sync.h"
#include "shared/rpc/player/script.h"
#include "shared/rpc/player/weapon.h"
#include "shared/rpc/world_space.h"
#include "shared/sync_rates.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/world/world_space.h"

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <utils/interpolator.h>

#include <glm/gtc/quaternion.hpp>

#include <unordered_map>
#include <unordered_set>

#include <algorithm>
#include <cmath>

namespace VantaMP::Game {
    using namespace PlayerSyncState;

    namespace {
        namespace Replication = Framework::Networking::Replication;


        Shared::PlayerEntity *OwnedPlayer(Replication::ReplicationManager *replication) {
            Shared::PlayerEntity *owned = nullptr;

            if (!replication) {
                return nullptr;
            }

            replication->ForEachEntity([&owned](Replication::NetworkEntity *entity) {
                auto *player = dynamic_cast<Shared::PlayerEntity *>(entity);

                if (player && player->IsOwner()) {
                    owned = player;
                }
            });

            return owned;
        }















    } // namespace

    void UpdatePlayerSync() {
        PulseWorldEnv();

        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication) {
            return;
        }

        // Which entities are still here, so the peds of the ones that are not
        // can be removed. Collected during the walk rather than from a destroy
        // callback: an entity can vanish for reasons other than a disconnect -
        // streaming out is the common one - and every reason looks the same to
        // the ped, which has to go either way.
        // REUSED, NOT REBUILT. This runs every frame and used to allocate a
        // fresh hash set each time purely to answer "did I see this entity
        // this tick". Clearing keeps the buckets, so after the first few
        // frames it stops touching the allocator entirely.
        static std::unordered_set<std::uint64_t> seen;

        seen.clear();

        replication->ForEachEntity([](Replication::NetworkEntity *entity) {
            auto *player = dynamic_cast<Shared::PlayerEntity *>(entity);

            if (!player) {
                return;
            }

            if (player->IsOwner()) {
                gLocalNetworkId = static_cast<std::uint64_t>(player->GetNetworkID());

                // Pick up an id that was announced before we knew which entity
                // was ours. Cheap: a hash lookup once a tick until it resolves,
                // and nothing at all afterwards.
                if (!gLocalWireId.Valid()) {
                    gLocalWireId = gPlayerWireIds.Find(gLocalNetworkId);
                }

                PushLocalState(player);
                CheckLocalDeath();
                return;
            }

            // Other dimension: leave them out of `seen` so the stream-out
            // path below destroys a ped that was already here.
            if (!Shared::SameDimension(gLocalWorld.dimension, player->dimension)) {
                return;
            }

            seen.insert(static_cast<std::uint64_t>(player->GetNetworkID()));
            ApplyRemoteState(player);
        });


        for (auto it = gSlotByNetworkId.begin(); it != gSlotByNetworkId.end();) {
            if (seen.contains(it->first)) {
                ++it;
                continue;
            }

            gInterpolationBySlot.erase(it->second);
            gRemoteAlpha.erase(it->second);
            gAimedBySlot.erase(it->second);

            // The wire id and the applied sequence die with the player.
            // Leaving the mapping behind would let a packet still in flight
            // resolve to a player who is gone, and leaving the sequence would
            // make a REJOINING player's first packets look stale until their
            // counter climbed past the old one.
            gPlayerWireIds.Release(it->first);
            gAppliedSequence.erase(it->first);

            ClearRemoteOccupancy(it->second);
            SDK::DestroyRemotePed(it->second);
            it = gSlotByNetworkId.erase(it);
        }
    }


    void InstallRemotePlayerCorrection() {
        SetRemotePedCorrector(&CorrectRemotePlayer);

        Shared::gOnPlayerForced = [](Shared::PlayerEntity *player) {
            if (!player || !player->IsOwner()) {
                return;
            }

            SDK::SetLocalPedTransform(player->position.x, player->position.y, player->position.z, glm::roll(player->rotation));
        };

        Shared::gOnVehicleForced = [](Shared::VehicleEntity *vehicle) {
            if (!vehicle || !vehicle->IsOwner()) {
                return;
            }

            if (auto *handle = VehicleHandleOf(static_cast<std::uint64_t>(vehicle->GetNetworkID()))) {
                SDK::SetVehiclePose(handle, vehicle->position.x, vehicle->position.y, vehicle->position.z, vehicle->rotation);
                SDK::ApplyVehicleDamage(handle, vehicle->damageEngine, vehicle->doorsPacked, vehicle->wheelsPacked, vehicle->panels, vehicle->lights);
                SDK::ApplyVehicleDoorRatios(handle, vehicle->doorRatiosPacked, SDK::OccupiedDoorMask(handle));
                SDK::ApplyVehicleScriptState(handle, vehicle->engineOn, vehicle->sirenOn, vehicle->overrideLights, vehicle->locked);
                SDK::ApplyVehicleExtras(handle, vehicle->dirtLevel, vehicle->taxiLight, vehicle->headlightColor, vehicle->frozen, vehicle->damageProof,
                                        vehicle->collisions, vehicle->landingGearDown, vehicle->lightsOn, vehicle->engineBroken, vehicle->wheelsCanBurst,
                                        vehicle->canBeDamaged, vehicle->nitroCount);
                SDK::SetVehiclePlateText(handle, vehicle->plateText);
                SDK::SetVehicleAlpha(handle, vehicle->alpha);

                if (vehicle->IsBlown()) {
                    SDK::WreckVehicle(handle, true);
                }

                SDK::ApplyVehicleStates(handle, vehicle->health, vehicle->engineOn, vehicle->sirenOn);
            }
        };
    }

    void ClearRemotePlayers() {
        for (const auto &[networkId, slot] : gSlotByNetworkId) {
            ClearRemoteOccupancy(slot);
            SDK::DestroyRemotePed(slot);
        }

        gSlotByNetworkId.clear();
        gInterpolationBySlot.clear();
        gAimedBySlot.clear();
        gRemoteAlpha.clear();

        // A disconnect invalidates every mapping: the next server mints its
        // own id space, and our own id is no longer ours.
        gPlayerWireIds.Clear();
        gAppliedSequence.clear();
        gLocalWireId = Shared::RPC::Common::WireId {};
        gLocalSend   = LocalSendState {};
    }

    int SlotOfNetworkId(std::uint64_t networkId) {
        const auto found = gSlotByNetworkId.find(networkId);

        return found != gSlotByNetworkId.end() ? found->second : -1;
    }

    std::uint64_t NetworkIdOfSlot(int slot) {
        for (const auto &[networkId, occupied] : gSlotByNetworkId) {
            if (occupied == slot) {
                return networkId;
            }
        }

        return 0;
    }

    std::uint64_t LocalNetworkId() {
        return gLocalNetworkId;
    }

    LocalWorldSpace QueryLocalWorldSpace() {
        return gLocalWorld;
    }

    void ApplyOwnedWorldSpace(std::uint8_t interior, std::uint16_t dimension, std::uint8_t flags) {
        if ((flags & Shared::kWorldSpaceInterior) != 0) {
            SDK::SetCurrentInterior(interior);
            SDK::SetLocalPedInterior(interior);
            gLocalWorld.interior = interior;
        }

        if ((flags & Shared::kWorldSpaceDimension) != 0) {
            gLocalWorld.dimension = dimension;
        }

        if (auto *player = OwnedPlayer(Framework::CoreModules::GetReplication())) {
            if ((flags & Shared::kWorldSpaceInterior) != 0) {
                player->interior = interior;
            }

            if ((flags & Shared::kWorldSpaceDimension) != 0) {
                player->dimension = dimension;
            }
        }
    }

    void ApplyWorldSpace(const Shared::RPC::WorldSpace &space) {
        ApplyOwnedVehicleWorldSpace(space.networkId, space.interior, space.dimension, space.flags);

        auto *replication = Framework::CoreModules::GetReplication();
        auto *player      = OwnedPlayer(replication);

        if (!player || static_cast<std::uint64_t>(player->GetNetworkID()) != space.networkId) {
            if (space.networkId == 0 || space.networkId == gLocalNetworkId) {
                ApplyOwnedWorldSpace(space.interior, space.dimension, space.flags);
            }

            return;
        }

        ApplyOwnedWorldSpace(space.interior, space.dimension, space.flags);
    }

    void ApplyPlayerScript(const Shared::RPC::PlayerScript &script) {
        if ((script.flags & Shared::kPlayerScriptHealth) != 0 || (script.flags & Shared::kPlayerScriptArmour) != 0) {
            const auto local = SDK::QueryLocalPed();

            SDK::SetLocalPedVitals((script.flags & Shared::kPlayerScriptHealth) != 0 ? script.health : local.health,
                                   (script.flags & Shared::kPlayerScriptArmour) != 0 ? script.armour : local.armour);
        }

        if ((script.flags & Shared::kPlayerScriptModel) != 0 && script.modelId != 0) {
            SDK::SetLocalPedModel(static_cast<int>(script.modelId));
        }

        if ((script.flags & Shared::kPlayerScriptMoney) != 0) {
            SDK::SetLocalPlayerMoney(script.money);
        }

        if ((script.flags & Shared::kPlayerScriptWanted) != 0) {
            SDK::SetLocalPlayerWanted(script.wantedLevel);
        }

        if ((script.flags & Shared::kPlayerScriptFrozen) != 0) {
            SDK::SetLocalPedFrozen(script.frozen);
        }

        if ((script.flags2 & Shared::kPlayerScript2Collisions) != 0) {
            SDK::SetLocalPedCollision(script.collisions);
        }

        if ((script.flags2 & Shared::kPlayerScript2FightingStyle) != 0) {
            SDK::SetLocalPedFightingStyle(script.fightingStyle);
        }

        if ((script.flags2 & Shared::kPlayerScript2Oxygen) != 0) {
            SDK::SetLocalPlayerOxygen(script.oxygen);
        }

        if ((script.flags2 & Shared::kPlayerScript2WalkingStyle) != 0) {
            SDK::SetLocalPedWalkingStyle(script.walkingStyle);
        }

        if ((script.flags2 & Shared::kPlayerScript2Alpha) != 0) {
            SDK::SetLocalPedAlpha(script.alpha);
        }

        if ((script.flags2 & Shared::kPlayerScript2OnFire) != 0) {
            SDK::SetLocalPedOnFire(script.onFire);
        }

        if ((script.flags2 & Shared::kPlayerScript2MaxHealth) != 0) {
            SDK::SetLocalPedMaxHealth(script.maxHealth);
        }

        if ((script.flags2 & Shared::kPlayerScript2HasJetPack) != 0) {
            SDK::SetLocalPedJetPack(script.hasJetPack);
        }

        if (auto *player = OwnedPlayer(Framework::CoreModules::GetReplication())) {
            if ((script.flags & Shared::kPlayerScriptHealth) != 0) {
                player->health = script.health;
            }

            if ((script.flags & Shared::kPlayerScriptArmour) != 0) {
                player->armour = script.armour;
            }

            if ((script.flags & Shared::kPlayerScriptModel) != 0) {
                player->modelId = script.modelId;
            }

            if ((script.flags & Shared::kPlayerScriptMoney) != 0) {
                player->money = script.money;
            }

            if ((script.flags & Shared::kPlayerScriptWanted) != 0) {
                player->wantedLevel = script.wantedLevel;
            }

            if ((script.flags & Shared::kPlayerScriptFrozen) != 0) {
                player->frozen = script.frozen;
            }

            if ((script.flags2 & Shared::kPlayerScript2Collisions) != 0) {
                player->collisions = script.collisions;
            }

            if ((script.flags2 & Shared::kPlayerScript2FightingStyle) != 0) {
                player->fightingStyle = script.fightingStyle;
            }

            if ((script.flags2 & Shared::kPlayerScript2Oxygen) != 0) {
                player->oxygen = script.oxygen;
            }

            if ((script.flags2 & Shared::kPlayerScript2WalkingStyle) != 0) {
                player->walkingStyle = script.walkingStyle;
            }

            if ((script.flags2 & Shared::kPlayerScript2Alpha) != 0) {
                player->alpha = script.alpha;
            }

            if ((script.flags2 & Shared::kPlayerScript2OnFire) != 0) {
                player->onFire = script.onFire;
            }

            if ((script.flags2 & Shared::kPlayerScript2MaxHealth) != 0) {
                player->maxHealth = script.maxHealth;
            }

            if ((script.flags2 & Shared::kPlayerScript2HasJetPack) != 0) {
                player->hasJetPack = script.hasJetPack;
            }
        }
    }

    void AdoptPlayerWireId(std::uint64_t networkId, std::uint16_t wireId) {
        gPlayerWireIds.Adopt(networkId, Shared::RPC::Common::WireId {wireId});

        // OUR OWN ID MAY ARRIVE BEFORE WE KNOW WHICH ENTITY IS OURS.
        //
        // The server announces wire ids from OnPlayerReady, which can land
        // before UpdatePlayerSync has run and discovered the owned entity - so
        // gLocalNetworkId is still 0 and a `networkId == gLocalNetworkId` test
        // would miss. The mapping is kept regardless (above), and
        // ResolveLocalWireId below picks it up once the entity appears, so the
        // two can arrive in either order.
        if (gLocalNetworkId != 0 && networkId == gLocalNetworkId) {
            gLocalWireId = Shared::RPC::Common::WireId {wireId};
        }
    }

    void SetLocalPlayerWireId(std::uint16_t wireId) {
        gLocalWireId = Shared::RPC::Common::WireId {wireId};
    }

    namespace {
        // Resolves a wire id to the entity it names, refusing our own.
        //
        // The server broadcasts, so our own packets come back to us; applying
        // them would fight the local simulation with a 100 ms-old copy of it.
        [[nodiscard]] Shared::PlayerEntity *ResolveRemotePlayer(Shared::RPC::Common::WireId wireId, std::uint64_t &networkIdOut) {
            networkIdOut = gPlayerWireIds.Resolve(wireId);

            // A wire id that does not resolve names a player who has left, or
            // was minted before a slot was reused. Dropping it is the point of
            // the generation half of the id.
            if (networkIdOut == 0 || networkIdOut == gLocalNetworkId) {
                return nullptr;
            }

            auto *replication = Framework::CoreModules::GetReplication();

            if (!replication) {
                return nullptr;
            }

            auto *entity = dynamic_cast<Shared::PlayerEntity *>(replication->GetEntityByNetworkID(static_cast<MafiaNet::NetworkID>(networkIdOut)));

            return (entity && !entity->IsOwner()) ? entity : nullptr;
        }
    } // namespace

    void ApplyRemoteOnFootSync(const Shared::RPC::PlayerOnFootSync &sync) {
        namespace Field = Shared::RPC::Player::OnFootField;

        std::uint64_t networkId = 0;
        auto         *entity    = ResolveRemotePlayer(sync.playerId, networkId);

        if (!entity) {
            return;
        }

        auto &seen = gAppliedSequence[networkId];

        // LATEST WINS. The stream is Unreliable, so datagrams reorder: an
        // older snapshot applied after a newer one makes the ped jump back.
        if (seen.haveOnFoot && !Shared::RPC::IsNewerSequence(sync.sequence, seen.onFoot)) {
            return;
        }

        seen.onFoot     = sync.sequence;
        seen.haveOnFoot = true;

        // ONLY WHAT THE MASK NAMES. A suppressed field keeps the value we
        // hold - the sender suppressed it BECAUSE it still matches that
        // value, so overwriting it with a default would be the stale-state
        // bug this protocol is built to avoid.
        if (sync.Has(Field::kPosition)) {
            entity->position = sync.position;
        }

        if (sync.Has(Field::kHeading)) {
            entity->rotation = HeadingTo(sync.heading);
        }

        if (sync.Has(Field::kVelocity)) {
            entity->velocity = sync.velocity;
        }

        if (sync.Has(Field::kInput)) {
            entity->leftStickX     = sync.leftStickX;
            entity->leftStickY     = sync.leftStickY;
            entity->accelerator    = sync.accelerator;
            entity->brake          = sync.brake;
            entity->buttons        = sync.buttons;
            entity->cameraRotation = sync.cameraRotation;
        }

        // Aim is present only when the sender was actually aiming; absent
        // means "not aiming", NOT "aiming at zero".
        if (sync.Has(Field::kAim)) {
            entity->aimHorizontal = sync.aimHorizontal;
            entity->aimVertical   = sync.aimVertical;
        }

        if (sync.Has(Field::kFlags)) {
            namespace Flag = Shared::RPC::Player::PedFlag;

            entity->isDucked  = (sync.flags & Flag::kDucked) != 0;
            entity->isInAir   = (sync.flags & Flag::kInAir) != 0;
            entity->isInWater = (sync.flags & Flag::kInWater) != 0;
        }

        // ApplyRemoteState picks these up from the entity on the next frame,
        // exactly as it did when the replica carried them. Nothing below this
        // point knows the difference.
    }


    void ApplyPlayerWeapon(std::uint8_t action, std::uint32_t weaponType, std::uint32_t ammo) {
        if (action == Shared::RPC::PlayerWeapon::kTakeAll) {
            SDK::TakeAllLocalPedWeapons();
            return;
        }

        if (action == Shared::RPC::PlayerWeapon::kTake) {
            SDK::TakeLocalPedWeapon(weaponType);
            return;
        }

        if (action == Shared::RPC::PlayerWeapon::kSetAmmo) {
            SDK::SetLocalPedWeaponAmmo(weaponType, ammo);
            return;
        }

        if (action == Shared::RPC::PlayerWeapon::kSetSlot) {
            SDK::SetLocalPedWeaponSlot(weaponType);
            return;
        }

        SDK::GiveLocalPedWeapon(weaponType, ammo);
    }
} // namespace VantaMP::Game
