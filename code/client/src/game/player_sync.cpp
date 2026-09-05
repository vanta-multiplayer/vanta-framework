#include "player_sync.h"

#include "player_slots.h"
#include "remote_players.h"
#include "sdk/game.h"
#include "shared/player_entity.h"

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>

#include <unordered_map>
#include <unordered_set>

namespace VantaMP::Game {
    namespace {
        namespace Replication = Framework::Networking::Replication;

        // Which slot each remote entity was given a ped in. Keyed by network id
        // rather than by pointer: an entity can be destroyed and its memory
        // reused, and a stale pointer would silently match the wrong player.
        std::unordered_map<std::uint64_t, int> gSlotByNetworkId;

        // The default player model. CJ, which is what the local player wears
        // until a server says otherwise.
        constexpr int kDefaultModel = 7;

        // Remote players start at slot 2.
        //
        // SLOT 1 IS SKIPPED DELIBERATELY. It is a leftover from the game's
        // two-player co-op mode and is not treated like the others - it has its
        // own handling in places, which shows up as one player behaving
        // differently to everyone else for no visible reason.
        constexpr int kFirstRemoteSlot = 2;

        int ClaimSlot() {
            for (int slot = kFirstRemoteSlot; slot < kMaxPlayers; ++slot) {
                if (!PedInSlot(slot)) {
                    return slot;
                }
            }

            return -1;
        }

        // A quaternion down to the one angle GTA cares about.
        //
        // The wire carries a full rotation because that is what the framework's
        // transform tier replicates, but a ped on foot only ever yaws - so
        // anything else in it describes a pose the game cannot adopt anyway.
        float HeadingFrom(const glm::quat &rotation) {
            return glm::yaw(rotation);
        }

        glm::quat HeadingTo(float heading) {
            return glm::quat(glm::vec3(0.0f, 0.0f, heading));
        }

        // Copies the local player into the entity the server sees.
        void PushLocalState(Shared::PlayerEntity *entity) {
            const auto local = SDK::QueryLocalPed();

            if (!local.valid) {
                return;
            }

            entity->position = glm::vec3(local.x, local.y, local.z);
            entity->rotation = HeadingTo(local.heading);

            entity->health = local.health;

            // Velocity is left alone. The transform tier carries it, but reading
            // it from the ped means another SDK round trip for a value the
            // receiving side does not use: remote peds are moved by their input,
            // not by integrating a velocity we sent them.

            entity->modelId = static_cast<std::uint32_t>(local.modelIndex);
        }

        // Applies a remote entity to its ped, creating one if needed.
        void ApplyRemoteState(Shared::PlayerEntity *entity) {
            const auto networkId = static_cast<std::uint64_t>(entity->GetNetworkID());

            auto found = gSlotByNetworkId.find(networkId);

            if (found == gSlotByNetworkId.end()) {
                const int slot = ClaimSlot();

                if (slot < 0) {
                    Framework::Logging::GetLogger("Sync")->warn("No free slot for player {}; they will be invisible", networkId);
                    return;
                }

                const int model = entity->modelId != 0 ? static_cast<int>(entity->modelId) : kDefaultModel;

                if (!SDK::CreateRemotePed(slot, model, entity->position.x, entity->position.y, entity->position.z)) {
                    Framework::Logging::GetLogger("Sync")->error("Could not create a ped for player {}", networkId);
                    return;
                }

                found = gSlotByNetworkId.emplace(networkId, slot).first;

                Framework::Logging::GetLogger("Sync")->info("Player {} ('{}') appeared in slot {}", networkId, entity->nickname, slot);
            }

            const int slot = found->second;

            // POSITION IS WRITTEN, MOVEMENT IS NOT.
            //
            // The ped is moved to where the owner says it is, and the game's own
            // processing does the walking - driven by the input in the entity,
            // which the remote-player hooks feed to it. Writing an animation
            // state here instead would mean every client agreeing about a pose
            // none of them computed.
            SDK::SetRemotePedTransform(slot, entity->position.x, entity->position.y, entity->position.z, HeadingFrom(entity->rotation));

            SDK::SetRemotePedHealth(slot, entity->health, entity->armour);

            // The input the game will act on next time it processes this ped.
            if (auto *pad = InputForSlot(slot)) {
                // The first two bytes of CPad are the left stick, which is what
                // GTA's movement reads. Written as raw bytes because the layout
                // belongs to plugin-sdk and nothing here needs the rest of it.
                auto *bytes = static_cast<std::uint8_t *>(pad);

                *reinterpret_cast<std::int16_t *>(bytes + 0) = entity->leftStickX;
                *reinterpret_cast<std::int16_t *>(bytes + 2) = entity->leftStickY;
            }
        }
    } // namespace

    void UpdatePlayerSync() {
        auto *replication = Framework::CoreModules::GetReplication();

        if (!replication) {
            return;
        }

        // Which entities are still here, so the peds of the ones that are not
        // can be removed. Collected during the walk rather than from a destroy
        // callback: an entity can vanish for reasons other than a disconnect -
        // streaming out is the common one - and every reason looks the same to
        // the ped, which has to go either way.
        std::unordered_set<std::uint64_t> seen;

        replication->ForEachEntity([&seen](Replication::NetworkEntity *entity) {
            auto *player = dynamic_cast<Shared::PlayerEntity *>(entity);

            if (!player) {
                return;
            }

            if (player->IsOwner()) {
                PushLocalState(player);
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

            Framework::Logging::GetLogger("Sync")->info("Player {} left slot {}", it->first, it->second);

            SDK::DestroyRemotePed(it->second);
            it = gSlotByNetworkId.erase(it);
        }
    }

    void ClearRemotePlayers() {
        for (const auto &[networkId, slot] : gSlotByNetworkId) {
            SDK::DestroyRemotePed(slot);
        }

        gSlotByNetworkId.clear();
    }
} // namespace VantaMP::Game
