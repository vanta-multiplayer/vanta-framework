#include "game/sync/player_sync.h"

#include "game/entities/player_slots.h"
#include "game/entities/remote_players.h"
#include "sdk/game.h"
#include "shared/player_entity.h"
#include "shared/sync_rates.h"

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/replication_manager.h>
#include <utils/interpolator.h>

#include <glm/gtc/quaternion.hpp>

#include <unordered_map>
#include <unordered_set>

#include <algorithm>

namespace VantaMP::Game {
    namespace {
        namespace Replication = Framework::Networking::Replication;

        // Which slot each remote entity was given a ped in. Keyed by network id
        // rather than by pointer: an entity can be destroyed and its memory
        // reused, and a stale pointer would silently match the wrong player.
        std::unordered_map<std::uint64_t, int> gSlotByNetworkId;

        // One per remote ped.
        //
        // THE FRAMEWORK'S, NOT ONE OF OUR OWN. It implements the same error
        // compensation MTA does - correct the DIFFERENCE between where the game
        // has put an entity and where its owner says it is, a share at a time,
        // rather than lerping the position and fighting the game's own physics.
        // It also carries rotation as a quaternion slerp and scales the
        // compensation by how late an update was, neither of which a
        // reimplementation here would have had.
        //
        // Vehicles will want exactly this too, which is the other reason not to
        // grow a private copy in the player path.
        std::unordered_map<int, Framework::Utils::Interpolator> gInterpolationBySlot;

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

            // WHAT MAKES A REMOTE PLAYER WALK RATHER THAN GLIDE. GTA derives
            // animation, speed, crouching and strafing from controller input,
            // so replaying it on the other client reproduces the movement with
            // a correct walk cycle. Position alone leaves the player sliding
            // with their legs still on every other screen.
            const auto input = SDK::ReadLocalInput();

            entity->leftStickX  = input.leftStickX;
            entity->leftStickY  = input.leftStickY;
            entity->accelerator = input.accelerator;
            entity->brake       = input.brake;
            entity->buttons     = input.buttons;
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

            // AIMED AT, NOT SNAPPED TO.
            //
            // The ped is given a target and a deadline, and moved towards it a
            // frame at a time by InterpolateRemotePlayers. Writing the position
            // here instead puts the ped in a new place ten times a second and
            // leaves it still in between - which is what makes a remote player
            // read as teleporting rather than walking.
            //
            // The game's own processing does the walking, driven by the input in
            // the entity; this only corrects where that walking has drifted from
            // what the owner reports.
            const auto current = SDK::GetRemotePedPosition(slot);

            auto &interpolator = gInterpolationBySlot[slot];

            interpolator.GetPosition()->SetTargetValue(glm::vec3(current.x, current.y, current.z), entity->position, static_cast<float>(Shared::SyncRate::kPlayerMs));

            interpolator.GetRotation()->SetTargetValue(SDK::GetRemotePedRotation(slot), entity->rotation, static_cast<float>(Shared::SyncRate::kPlayerMs));


            SDK::SetRemotePedHealth(slot, entity->health, entity->armour);

            // The input the game will act on next time it processes this ped.
            SDK::ControllerInput input;
            input.leftStickX  = entity->leftStickX;
            input.leftStickY  = entity->leftStickY;
            input.accelerator = entity->accelerator;
            input.brake       = entity->brake;
            input.buttons     = entity->buttons;

            SDK::WriteRemoteInput(slot, input);
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

            gInterpolationBySlot.erase(it->second);

            SDK::DestroyRemotePed(it->second);
            it = gSlotByNetworkId.erase(it);
        }
    }

    void InterpolateRemotePlayers() {
        for (auto &[slot, interpolator] : gInterpolationBySlot) {
            const auto current = SDK::GetRemotePedPosition(slot);
            const glm::vec3 here(current.x, current.y, current.z);

            const auto next = interpolator.GetPosition()->UpdateTargetValue(here);

            SDK::SetRemotePedPosition(slot, next.x, next.y, next.z);

            // Rotation follows the same correction. Written through the game's
            // own rotation fields rather than the matrix, so a ped on the
            // ground and one in the air both end up facing the right way.
            const auto rotation = interpolator.GetRotation()->UpdateTargetValue(SDK::GetRemotePedRotation(slot));

            SDK::SetRemotePedHeading(slot, glm::yaw(rotation));
        }
    }

    void ClearRemotePlayers() {
        for (const auto &[networkId, slot] : gSlotByNetworkId) {
            SDK::DestroyRemotePed(slot);
        }

        gSlotByNetworkId.clear();
        gInterpolationBySlot.clear();
    }
} // namespace VantaMP::Game
