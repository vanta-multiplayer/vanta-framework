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

        // The last state actually aimed at, per slot.
        //
        // UpdatePlayerSync runs EVERY FRAME but state arrives ten times a
        // second, so five frames in six carry the same numbers as the last.
        // Re-aiming on those restarts the interpolation from wherever the ped
        // has got to, towards the same target, with a fresh deadline - so it
        // never finishes converging and the ped trails its owner permanently.
        //
        // Comparing against what was last aimed at is what makes a new target
        // new. Position and rotation together, because either can change alone.
        struct AimedAt {
            glm::vec3 position {};
            glm::quat rotation {};

            // What the owner was doing when they reported it, for the warp
            // threshold - a sprinter covers more ground between two updates
            // than a walker, so the gap that counts as absurd is not fixed.
            float speed = 0.0f;

            // When this slot was last aimed, so it can be re-aimed on a cadence
            // rather than only when the numbers change.
            std::uint32_t whenMs = 0;

            // How many updates this slot has had. Counts the velocity gating
            // below, which follows MTA in applying move speed on the ground only
            // every fourth update.
            std::uint32_t updates = 0;

            // The heading this ped is turning FROM and TO, and the window it has
            // to do it in. See the note where it is advanced.
            float headingFrom = 0.0f;
            float headingTo   = 0.0f;

            std::uint32_t headingBeginMs = 0;
            std::uint32_t headingEndMs   = 0;

            bool valid = false;
        };

        std::unordered_map<int, AimedAt> gAimedBySlot;

        // TEMPORARY TELEMETRY - remove once the desync is closed.
        struct Telemetry {
            int newTargets  = 0; // how often a genuinely new state arrived
            int applies     = 0; // how often ApplyRemoteState ran at all
            int corrections = 0; // how often the per-ped correction ran
        int warps       = 0; // how often the gap was big enough to snap
        int swimStarts  = 0; // how often a swim task had to be installed
        int velocities  = 0; // how often move speed was written over the game's

        float worstWarp = 0.0f; // and the biggest gap that caused one

            glm::vec3 lastTarget {};
            glm::vec3 lastPedPosition {};

            float worstError = 0.0f;

            float targetHeading = 0.0f;
            float pedHeading    = 0.0f;
        };

        std::unordered_map<int, Telemetry> gTelemetryBySlot;

        std::uint32_t gLastTelemetryMs = 0;

        void ReportTelemetry() {
            const std::uint32_t now = GetTickCount();

            if (now - gLastTelemetryMs < 1000u) {
                return;
            }

            gLastTelemetryMs = now;

            constexpr float kDegrees = 57.2957795f;

            for (auto &[slot, t] : gTelemetryBySlot) {
                const float error = glm::distance(t.lastTarget, t.lastPedPosition);

                Framework::Logging::GetLogger("SyncDbg")
                    ->info("RECV slot {} | new {}/s corr {}/s warp {}/s ({:.2f}) swim {}/s vel {}/s | target {:.2f},{:.2f} ped {:.2f},{:.2f} err {:.2f} worst {:.2f} | headTarget {:.1f} headPed {:.1f}", slot, t.newTargets, t.corrections, t.warps, t.worstWarp, t.swimStarts, t.velocities, t.lastTarget.x, t.lastTarget.y, t.lastPedPosition.x, t.lastPedPosition.y, error, t.worstError, t.targetHeading * kDegrees, t.pedHeading * kDegrees);

                t.newTargets  = 0;
                t.applies     = 0;
                t.corrections = 0;
                t.warps       = 0;
                t.swimStarts  = 0;
                t.velocities  = 0;
                t.worstError  = 0.0f;
                t.worstWarp   = 0.0f;
            }
        }

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
            // glm::ROLL, NOT glm::yaw.
            //
            // GLM names euler angles by AXIS, not by what they mean for a
            // character: pitch is X, yaw is Y, roll is Z. GTA's heading is a
            // rotation about Z, so the accessor that reads it back is roll.
            //
            // glm::yaw here is not merely inaccurate, it is identically zero
            // for a Z-only quaternion - so every remote ped faced due north
            // regardless of where its owner was looking.
            return glm::roll(rotation);
        }

        glm::quat HeadingTo(float heading) {
            return glm::quat(glm::vec3(0.0f, 0.0f, heading));
        }

        // The signed short way round from one heading to another.
        //
        // THE SHORT WAY, because headings wrap. A ped at 179 degrees told to
        // face -179 has moved two degrees, not three hundred and fifty eight,
        // and interpolating the plain difference would spin it the whole way
        // round the wrong way once per crossing.
        //
        // This is MTA's GetOffsetRadians, which its rotation interpolation is
        // built on.
        float OffsetRadians(float from, float to) {
            constexpr float kPi  = 3.14159265358979323846f;
            constexpr float kTau = 2.0f * kPi;

            float offset = to - from;

            if (offset > kPi) {
                offset -= kTau;
            } else if (offset <= -kPi) {
                offset += kTau;
            }

            return offset;
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

            const auto weapon = SDK::QueryLocalPedWeapon();

            entity->weaponType = weapon.type;
            entity->ammoClip   = weapon.ammoClip;
            entity->ammoTotal  = weapon.ammoTotal;

            // VELOCITY IS SENT, AND IT IS NOT REDUNDANT WITH THE INPUT.
            //
            // A remote ped given only input and a position has no momentum of
            // its own: between two updates it is dragged by the correction
            // alone, so it trails its owner by however far they travelled in
            // that interval - metres, at 100ms. Handing the game a move speed
            // lets the ped carry itself between updates, which is what MTA does
            // (SetMoveSpeed before the position target) and what closes the gap.
            entity->velocity = glm::vec3(local.velocityX, local.velocityY, local.velocityZ);

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

            // SENT WITH THE INPUT, BECAUSE IT IS WHAT THE INPUT MEANS. A stick
            // pushed forward means "away from the camera", so the two are one
            // reading: pair this tick's stick with last tick's view and the
            // player moves in a direction neither of them described.
            entity->cameraRotation = SDK::ReadLocalCameraRotation();

            entity->isDucked  = local.ducked;
            entity->isInAir   = local.inAir;
            entity->isInWater = local.inWater;

            // TEMPORARY TELEMETRY - remove once the desync is closed.
            static std::uint32_t lastSendReportMs = 0;

            if (const std::uint32_t now = GetTickCount(); now - lastSendReportMs >= 1000u) {
                lastSendReportMs = now;

                Framework::Logging::GetLogger("SyncDbg")
                    ->info("SEND me {:.2f},{:.2f} head {:.1f} vel {:.2f},{:.2f} stick {},{} buttons {:#x} ducked {} air {} water {} weapon {} ammo {}/{}", local.x, local.y, local.heading * 57.2957795f, local.velocityX, local.velocityY, input.leftStickX, input.leftStickY, entity->buttons, local.ducked, local.inAir, local.inWater, weapon.type, weapon.ammoClip, weapon.ammoTotal);
            }
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
            // frame at a time by CorrectRemotePlayer. Writing the position
            // here instead puts the ped in a new place ten times a second and
            // leaves it still in between - which is what makes a remote player
            // read as teleporting rather than walking.
            //
            // The game's own processing does the walking, driven by the input in
            // the entity; this only corrects where that walking has drifted from
            // what the owner reports.
            // THE ORDER BELOW IS MTA'S, AND IT IS NOT ARBITRARY.
            //
            // ReadPlayerPuresync applies move speed, then health and armour,
            // then the position target and rotation, and only THEN the
            // controller state, camera and pose. Input applied before the
            // transform acts on the previous frame's position; velocity applied
            // after it fights the correction that was just aimed.

            // WHAT ARRIVES WITH AN UPDATE IS APPLIED ON AN UPDATE.
            //
            // Momentum, vitals and the transform are per-packet facts in MTA and
            // are applied once per packet there. Writing them every frame instead
            // reapplies the same ten-times-a-second numbers sixty times a second,
            // which for velocity means overwriting the move speed GTA just
            // computed from the walk animation - sixty times a second, against a
            // ped the game is already moving from the replayed input.
            //
            // Input, camera and pose stay per-frame below: the pad is consumed
            // each time the ped is processed, so it has to be rewritten.
            auto &aimed = gAimedBySlot[slot];

            auto &telemetry = gTelemetryBySlot[slot];

            ++telemetry.applies;
            telemetry.lastTarget    = entity->position;
            telemetry.targetHeading = HeadingFrom(entity->rotation);
            telemetry.pedHeading    = HeadingFrom(SDK::GetRemotePedRotation(slot));

            const std::uint32_t now = GetTickCount();

            // RE-AIMED ON A CADENCE, NOT ONLY WHEN THE NUMBERS CHANGE.
            //
            // Change alone is not enough. The error compensation pays off the
            // gap it measured when it was aimed and then stops, so whatever is
            // left when a player stops moving stays there for good - measured at
            // a third of a metre, permanent, and invisible to a change test
            // because nothing is changing. MTA re-aims every tick regardless,
            // which closes that residue on the next update.
            //
            // Not every frame either: state arrives ten times a second and the
            // game draws sixty, so re-aiming on a frame carrying the same
            // numbers restarts the window from wherever the ped has got to
            // towards the same place, and it never finishes converging.
            //
            // The cadence is the rate state is sent at, which is what MTA's
            // tick rate is doing.
            const bool changed = !aimed.valid || aimed.position != entity->position || aimed.rotation != entity->rotation;
            const bool due     = aimed.valid && (now - aimed.whenMs) >= Shared::SyncRate::kPlayerMs;

            if (changed || due) {
                ++telemetry.newTargets;
                ++aimed.updates;

                // 1. Momentum, but NOT ON EVERY UPDATE WHILE ON FOOT.
                //
                // A ped standing on the ground gets its move speed from its own
                // walk animation, recomputed every frame. Overwriting that with
                // the owner's reading fights it: the ped is being carried by the
                // replayed input already, so the injected speed is movement
                // counted twice, and the correction then drags it back.
                //
                // In the AIR there is no animation driving it and the game is
                // integrating gravity, so the owner's velocity is the only thing
                // that makes a jump arc match - it goes every update.
                //
                // On the ground it still goes occasionally, to catch drift the
                // input replay alone would not. MTA's exact rule: every sync
                // while airborne, every fourth otherwise.
                if (entity->isInAir || (aimed.updates % 4u) == 0u) {
                    SDK::SetRemotePedVelocity(slot, entity->velocity.x, entity->velocity.y, entity->velocity.z);
                    ++telemetry.velocities;
                }

                // 2. Health, armour, and what is in their hands.
                SDK::SetRemotePedHealth(slot, entity->health, entity->armour);

                // On the update path rather than per-frame. It costs nothing
                // when the weapon has not changed, but the frame path runs six
                // times per update and there is nothing here that needs it.
                SDK::SetRemotePedWeapon(slot, entity->weaponType, entity->ammoClip, entity->ammoTotal);

                // 3. The transform.

                const auto current = SDK::GetRemotePedPosition(slot);

                auto &interpolator = gInterpolationBySlot[slot];

                // FULL CORRECTION, NOT A QUARTER OF ONE.
                //
                // The framework scales the error by mix(0.25, 1.0, unlerp(min,
                // max, delay)), so at the default range of 100..400ms an update
                // arriving every 100ms lands exactly on the lower bound and gets
                // 0.25 - a quarter of the gap closed per update, for ever, which
                // is a remote player permanently metres behind where their owner
                // says they are. MTA scales the error by nothing at all:
                // vecError is target - current, whole.
                //
                // The range is a statement about the rates a project sends at,
                // and ours is 100ms. Putting the minimum below it puts our rate
                // inside the range rather than on its floor.
                interpolator.GetPosition()->SetErrorContributionDelayRange(0.0f, static_cast<float>(Shared::SyncRate::kPlayerMs));

                interpolator.GetPosition()->SetTargetValue(glm::vec3(current.x, current.y, current.z), entity->position, static_cast<float>(Shared::SyncRate::kPlayerMs));

                // Rotation is AIMED here and turned frame by frame in
                // CorrectRemotePlayer, the same shape as the position above.
                // From wherever the ped is actually facing now, so the turn
                // starts where the last one ended rather than from the last
                // value received.
                aimed.headingFrom    = HeadingFrom(SDK::GetRemotePedRotation(slot));
                aimed.headingTo      = HeadingFrom(entity->rotation);
                aimed.headingBeginMs = now;
                aimed.headingEndMs   = now + Shared::SyncRate::kPlayerMs;

                aimed.position = entity->position;
                aimed.rotation = entity->rotation;
                aimed.speed    = glm::length(entity->velocity);
                aimed.whenMs   = now;
                aimed.valid    = true;
            }

            // 4. Input, AFTER the transform, so the game walks the ped from
            //    where it now is rather than from where it was last frame.
            SDK::ControllerInput input;
            input.leftStickX  = entity->leftStickX;
            input.leftStickY  = entity->leftStickY;
            input.accelerator = entity->accelerator;
            input.brake       = entity->brake;
            input.buttons     = entity->buttons;

            SDK::WriteRemoteInput(slot, input);

            // 5. The view the input is resolved against. Held until this ped is
            //    processed, when it is swapped into the game's single global.
            SDK::SetRemoteCameraRotation(slot, entity->cameraRotation);

            // 6. Pose, LAST, and through the game's own tasks.
            //
            // NONE OF THESE ARE APPLIED BY WRITING THE FLAG, because all three
            // are outputs the game recomputes every frame from its own physics.
            // Each is either handed the task that produces it, or left alone.
            //
            // isInAir is left alone on purpose, and is not a gap. The ped is
            // falling because the game walked it off something; saying so again
            // changes nothing. Its real job is deciding whether velocity gets
            // applied above, which is what MTA uses its own copy for too.
            SDK::SetRemotePedDucked(slot, entity->isDucked);

            if (entity->isInWater && SDK::StartRemotePedSwimming(slot)) {
                ++telemetry.swimStarts;
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

        ReportTelemetry();

        for (auto it = gSlotByNetworkId.begin(); it != gSlotByNetworkId.end();) {
            if (seen.contains(it->first)) {
                ++it;
                continue;
            }

            Framework::Logging::GetLogger("Sync")->info("Player {} left slot {}", it->first, it->second);

            gInterpolationBySlot.erase(it->second);
            gAimedBySlot.erase(it->second);

            SDK::DestroyRemotePed(it->second);
            it = gSlotByNetworkId.erase(it);
        }
    }

    void CorrectRemotePlayer(int slot) {
        const auto found = gInterpolationBySlot.find(slot);

        if (found == gInterpolationBySlot.end()) {
            return;
        }

        auto &interpolator = found->second;

        // TURNED FRAME BY FRAME, NOT SNAPPED TEN TIMES A SECOND.
        //
        // Heading used to be written once per update, straight to the value
        // received. In between, the game turned the ped with its own animation,
        // and then every hundred milliseconds this put it back - so a remote
        // player who was turning visibly stepped round in tenths.
        //
        // IT SHOWED UP IN THE WATER FIRST because that is where a turn is all
        // there is to see: the swim animation yaws the whole body with no
        // footfalls to hide the seam, and the ped is moving slowly enough that
        // the step is not mistaken for speed.
        //
        // An earlier note here said rotation must NOT be interpolated, on the
        // grounds that the write and the read addressed different things - the
        // write went to m_fCurrentRotation and the read came off the ped's
        // matrix, which the game rebuilds from that field only during its own
        // processing, so a correction was always measured against a stale value
        // and never converged. That was true when it was written and is not any
        // more: HeadingOf reads m_fCurrentRotation now, the same field
        // SetPedRotation writes, which is what the ninety degree fix was.
        //
        // With the read honest, this is what MTA has always done - see
        // CClientPed::Interpolate, which lerps from the heading held when the
        // update arrived to the one it carried, across the send interval.
        {
            auto aiming = gAimedBySlot.find(slot);

            if (aiming != gAimedBySlot.end() && aiming->second.headingEndMs != 0) {
                auto &aimed = aiming->second;

                if (const std::uint32_t now = GetTickCount(); now < aimed.headingEndMs) {
                    const float span     = static_cast<float>(aimed.headingEndMs - aimed.headingBeginMs);
                    const float progress = static_cast<float>(now - aimed.headingBeginMs);
                    const float offset   = OffsetRadians(aimed.headingFrom, aimed.headingTo);

                    SDK::SetRemotePedHeading(slot, aimed.headingFrom + offset * (progress / span));
                } else {
                    // Landed exactly, rather than left wherever the last frame
                    // of the window happened to fall.
                    SDK::SetRemotePedHeading(slot, aimed.headingTo);

                    aimed.headingEndMs = 0;
                }
            }
        }

        // WHERE THE GAME JUST PUT THIS PED, read after its own processing has
        // walked it. The interpolator returns that position plus a share of the
        // error, so the walk is kept and only the drift from what the owner
        // reports is taken out.
        const auto current = SDK::GetRemotePedPosition(slot);
        const glm::vec3 here(current.x, current.y, current.z);

        // WARPED, NOT SLID, WHEN THE GAP IS ABSURD.
        //
        // Correcting a share of the error at a time is right for the metre or
        // so a player drifts between updates. It is wrong for a player who has
        // teleported, respawned, or come back from a stall: the ped would walk
        // the whole way there in full view, through walls, taking as long as the
        // distance demands.
        //
        // The threshold scales with how fast the owner was going, because a
        // sprinter legitimately covers far more ground between two updates than
        // someone walking, and a fixed number either warps them constantly or
        // lets a real teleport slide. MTA's rule, and its numbers.
        if (const auto aimed = gAimedBySlot.find(slot); aimed != gAimedBySlot.end() && aimed->second.valid) {
            const float threshold = Shared::Interpolation::WarpThresholdFor(aimed->second.speed, Shared::SyncRate::kPlayerMs);

            if (const float gap = glm::distance(here, aimed->second.position); gap > threshold) {
                const auto &target = aimed->second.position;

                // COUNTED BEFORE THE EARLY RETURN. Warping used to leave through
                // here without recording anything, so the one event most worth
                // seeing was the one the telemetry could not show.
                auto &warped = gTelemetryBySlot[slot];

                ++warped.warps;
                warped.worstWarp = std::max(warped.worstWarp, gap);

                SDK::SetRemotePedPosition(slot, target.x, target.y, target.z);

                // Aimed at itself, so the error left over from before the warp
                // is not paid off on top of a position that is already correct.
                interpolator.GetPosition()->SetTargetValue(target, target, static_cast<float>(Shared::SyncRate::kPlayerMs));

                return;
            }
        }

        const auto next = interpolator.GetPosition()->UpdateTargetValue(here);

        SDK::SetRemotePedPosition(slot, next.x, next.y, next.z);

        auto &telemetry = gTelemetryBySlot[slot];

        ++telemetry.corrections;
        telemetry.lastPedPosition = next;
        telemetry.worstError      = std::max(telemetry.worstError, glm::distance(next, telemetry.lastTarget));
    }

    void InstallRemotePlayerCorrection() {
        SetRemotePedCorrector(&CorrectRemotePlayer);
    }

    void ClearRemotePlayers() {
        for (const auto &[networkId, slot] : gSlotByNetworkId) {
            SDK::DestroyRemotePed(slot);
        }

        gSlotByNetworkId.clear();
        gInterpolationBySlot.clear();
        gAimedBySlot.clear();
    }
} // namespace VantaMP::Game
