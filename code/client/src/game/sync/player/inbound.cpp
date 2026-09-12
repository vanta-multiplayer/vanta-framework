#include "game/sync/player_sync.h"

#include "game/sync/player/state.h"
#include "game/sync/death.h"
#include "game/sync/vehicle_sync.h"
#include "game/sync/world_sync.h"
#include "game/entities/remote_players.h"
#include "sdk/game.h"
#include "shared/entity/player_entity.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/rpc/common/sequence.h"
#include "shared/rpc/player/onfoot_sync.h"
#include "shared/sync_rates.h"
#include "shared/world/world_space.h"

#include <utils/safe_win32.h>

#include <core_modules.h>
#include <logging/logger.h>

#include <algorithm>
#include <cmath>

// WHAT THIS CLIENT DOES WITH OTHER PLAYERS.
//
// Two halves, both here because they share the per-slot registry:
//
//   apply       runs on the sync tick - claim a slot, seat or unseat the ped,
//               aim the interpolators at the newest packet
//   correct     runs per FRAME from the ped-processing hook - eases the ped
//               toward that target a share of the error at a time
//
// AIMED AT, NOT SNAPPED TO: writing the position straight from the packet puts
// the ped somewhere new ten times a second and still in between, which is what
// reads as teleporting rather than walking.
//
// Split out of player_sync.cpp. Shared per-slot state is in player/state.h.
namespace VantaMP::Game::PlayerSyncState {
    void WriteLerpedRemotePad(int slot, AimedAt &aimed, const Shared::PlayerEntity *entity) {
        const std::uint32_t now = GetTickCount();

        const bool analogChanged = !aimed.analogAimed || aimed.stickXTo != static_cast<float>(entity->leftStickX) ||
            aimed.stickYTo != static_cast<float>(entity->leftStickY) || aimed.accelTo != static_cast<float>(entity->accelerator) ||
            aimed.brakeTo != static_cast<float>(entity->brake);

        if (!aimed.analogAimed) {
            aimed.stickXFrom = aimed.stickXTo = static_cast<float>(entity->leftStickX);
            aimed.stickYFrom = aimed.stickYTo = static_cast<float>(entity->leftStickY);
            aimed.accelFrom = aimed.accelTo = static_cast<float>(entity->accelerator);
            aimed.brakeFrom = aimed.brakeTo = static_cast<float>(entity->brake);
            aimed.analogBeginMs             = now;
            aimed.analogEndMs               = now;
            aimed.analogAimed               = true;
        }
        else if (analogChanged) {
            aimed.stickXFrom = LerpAmount(aimed.stickXFrom, aimed.stickXTo, aimed.analogBeginMs, aimed.analogEndMs, now);
            aimed.stickYFrom = LerpAmount(aimed.stickYFrom, aimed.stickYTo, aimed.analogBeginMs, aimed.analogEndMs, now);
            aimed.accelFrom  = LerpAmount(aimed.accelFrom, aimed.accelTo, aimed.analogBeginMs, aimed.analogEndMs, now);
            aimed.brakeFrom  = LerpAmount(aimed.brakeFrom, aimed.brakeTo, aimed.analogBeginMs, aimed.analogEndMs, now);
            aimed.stickXTo   = static_cast<float>(entity->leftStickX);
            aimed.stickYTo   = static_cast<float>(entity->leftStickY);
            aimed.accelTo    = static_cast<float>(entity->accelerator);
            aimed.brakeTo    = static_cast<float>(entity->brake);
            aimed.analogBeginMs = now;
            aimed.analogEndMs   = now + Shared::SyncRate::kPlayerMs;
        }

        SDK::ControllerInput input;
        input.leftStickX  = static_cast<std::int16_t>(std::lround(LerpAmount(aimed.stickXFrom, aimed.stickXTo, aimed.analogBeginMs, aimed.analogEndMs, now)));
        input.leftStickY  = static_cast<std::int16_t>(std::lround(LerpAmount(aimed.stickYFrom, aimed.stickYTo, aimed.analogBeginMs, aimed.analogEndMs, now)));
        input.accelerator = static_cast<std::uint8_t>(std::lround(LerpAmount(aimed.accelFrom, aimed.accelTo, aimed.analogBeginMs, aimed.analogEndMs, now)));
        input.brake       = static_cast<std::uint8_t>(std::lround(LerpAmount(aimed.brakeFrom, aimed.brakeTo, aimed.analogBeginMs, aimed.analogEndMs, now)));
        input.buttons     = entity->buttons;

        SDK::WriteRemoteInput(slot, input);
    }


    // Applies a remote entity to its ped, creating one if needed.
    // Finds this player's ped slot, creating the ped if they are new.
    //
    // Returns -1 when the player cannot be shown at all - no free slot, or
    // GTA refused the ped. `justCreated` tells the caller whether this is
    // the first frame for that ped, which is what decides a stream-in warp
    // rather than a walked entry.
    [[nodiscard]] int AcquireRemotePedSlot(Shared::PlayerEntity *entity, std::uint64_t networkId, bool &justCreated) {
        auto found = gSlotByNetworkId.find(networkId);

        justCreated = false;

        if (found == gSlotByNetworkId.end()) {
            const int slot = ClaimSlot();

            if (slot < 0) {
                Framework::Logging::GetLogger("Sync")->warn("No free slot for player {}; they will be invisible", networkId);
                return -1;
            }

            const int model = entity->modelId != 0 ? static_cast<int>(entity->modelId) : kDefaultModel;

            if (!SDK::CreateRemotePed(slot, model, entity->position.x, entity->position.y, entity->position.z)) {
                Framework::Logging::GetLogger("Sync")->error("Could not create a ped for player {}", networkId);
                return -1;
            }

            found       = gSlotByNetworkId.emplace(networkId, slot).first;
            justCreated = true;
        }

        return found->second;
    }

    // Applies everything that depends on whether this player is in a vehicle:
    // collision, health, model, script state, and seating or unseating the ped.
    //
    // Returns true when the ped is seated and the caller must NOT correct its
    // position - a seated ped is placed by its vehicle, not by us.
    [[nodiscard]] bool ApplyRemoteOccupancy(Shared::PlayerEntity *entity, int slot, bool justCreated) {
        // THE OWNER SAYS ON FOOT, OUR COPY IS STILL SEATED.
        //
        // This branch owns the ped completely - it returns before the
        // position correction below ever runs, on the reasoning that a
        // seated ped's position comes from the vehicle. That holds while
        // both machines agree.
        //
        // A knock-off breaks the agreement. The owner's rider is thrown
        // clear, so vehicleNetworkId goes to 0, but our copy is still in
        // the seat, so IsPedOccupyingVehicle is still true and we take
        // this branch anyway - and return without ever correcting their
        // position. The remote ped stayed welded to the bike, or stood
        // wherever the seat left them, drifting further every packet.
        //
        // MTA guards the same way round: CClientPed::SetPosition only
        // skips the write when GetRealOccupiedVehicle() is non-null
        // (CClientPed.cpp:1070, 1128) - the ped's ACTUAL vehicle, not the
        // one the network last mentioned. When those disagree the
        // position write goes through.
        //
        // So: only skip the position correction while the owner still
        // claims a seat. Once they report on foot, fall through and
        // correct them, whatever our local copy still believes.
        const bool ownerInVehicle = entity->vehicleNetworkId != 0;

        if (ownerInVehicle || SDK::IsPedOccupyingVehicle(slot)) {
            const auto occupancy = SDK::QueryPedOccupancy(slot);

            // Only a seated ped loses collision. Doing it while they
            // walk to the door freezes the enter task in place.
            SDK::SetRemotePedCollision(slot, entity->collisions && !occupancy.seated);
            SDK::SetRemotePedHealth(slot, entity->health, entity->armour);
            SDK::SetRemotePedModel(slot, entity->modelId != 0 ? static_cast<int>(entity->modelId) : 0);
            SDK::SetRemotePedFrozen(slot, entity->frozen);
            SDK::SetRemotePedFightingStyle(slot, entity->fightingStyle);
            SDK::SetRemotePedOxygen(slot, entity->oxygen);
            SDK::SetRemotePedWalkingStyle(slot, entity->walkingStyle);
            SDK::SetRemotePedOnFire(slot, entity->onFire);
            SDK::SetRemotePedJetPack(slot, entity->hasJetPack);
            SDK::SetRemotePedAlpha(slot, entity->alpha);
            gRemoteAlpha[slot] = entity->alpha;
            SDK::SetRemotePedWeapon(slot, entity->weaponType, entity->ammoClip, entity->ammoTotal);

            if (entity->vehicleNetworkId != 0) {
                // MTA: stream-in / late join warps. A ped that already
                // existed on foot is walked in by PlayerEnterVehicle.
                SeatRemoteInVehicle(slot, entity->vehicleNetworkId, entity->vehicleSeat, justCreated);
            }
            else if (occupancy.leaving || occupancy.entering) {
                // The RPC owns the animation. Warping here is why the
                // leave door did not always play: server Vacate clears
                // the id immediately, and this snapshot arrived mid-task.
            }
            else if (occupancy.seated) {
                // BEING JACKED. GTA is dragging them out through the
                // jacker's EnterCarAsDriver. LeaveCar here fights that
                // and leaves them stuck on the back of the car.
                //
                // ALSO: not while they are being thrown off. The exit RPC
                // already raised CEventKnockOffBike, which ejects them
                // over the next few frames. Calling ExitVehicle here as
                // well is a SECOND ejection racing the first - it builds a
                // LeaveCar over a ped mid-tumble, which is why the fall
                // looked right once and wrong afterwards.
                if (!SDK::IsAnyoneEnteringVehicleAsDriver(occupancy.vehicle) && !SDK::PedIsFallingOffBike(slot)) {
                    SDK::ExitVehicle(slot, false);
                    SDK::SetRemotePedCollision(slot, true);
                }
            }

            // THE PAD, EVEN IN A SEAT. Vehicle ProcessControl reads
            // GetPad(0), which we swap to this slot's pad. Without this
            // write the remote driver holds nothing and the wheels stay
            // straight - writing m_fSteerAngle does not survive that
            // recompute. Analog is lerped across the send interval so
            // a turn is a ramp, not a 100ms snap of the cabin.
            WriteLerpedRemotePad(slot, gAimedBySlot[slot], entity);
            SDK::SetRemotePedInterior(slot, entity->interior);

            // Only the vehicle owns this ped's position while the OWNER
            // agrees they are in it. Otherwise fall through to the
            // correction below - see the note above.
            if (ownerInVehicle || occupancy.entering || occupancy.leaving) {
                return true;
            }

        }
        else {
            SDK::SetRemotePedCollision(slot, true);
        }

        return false;
    }

    // Applies everything that is a PER-PACKET fact, on the packet edge only.
    //
    void ApplyRemoteSnapshot(Shared::PlayerEntity *entity, int slot, AimedAt &aimed, std::uint32_t now) {
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
        }

        // 2. Health, armour, and what is in their hands.
        SDK::SetRemotePedHealth(slot, entity->health, entity->armour);
        SDK::SetRemotePedModel(slot, entity->modelId != 0 ? static_cast<int>(entity->modelId) : 0);
        SDK::SetRemotePedFrozen(slot, entity->frozen);
        SDK::SetRemotePedCollision(slot, entity->collisions);
        SDK::SetRemotePedFightingStyle(slot, entity->fightingStyle);
        SDK::SetRemotePedOxygen(slot, entity->oxygen);
        SDK::SetRemotePedWalkingStyle(slot, entity->walkingStyle);
        SDK::SetRemotePedOnFire(slot, entity->onFire);
        SDK::SetRemotePedJetPack(slot, entity->hasJetPack);
        SDK::SetRemotePedAlpha(slot, entity->alpha);
        gRemoteAlpha[slot] = entity->alpha;

        // On the update path rather than per-frame. It costs nothing
        // when the weapon has not changed, but the frame path runs six
        // times per update and there is nothing here that needs it.
        SDK::SetRemotePedWeapon(slot, entity->weaponType, entity->ammoClip, entity->ammoTotal);

        if (entity->isReloading) {
            SDK::ReloadRemotePedWeapon(slot);
        }

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

        // Same window as heading. Starts from wherever the last lerp
        // left the value, so consecutive updates join up instead of
        // jumping back to the previous target.
        const auto shown = SDK::GetRemotePedAim(slot);

        aimed.cameraFrom  = SDK::GetRemoteCameraRotation(slot);
        aimed.cameraTo    = entity->cameraRotation;
        aimed.aimHFrom    = shown.horizontal;
        aimed.aimHTo      = entity->aimHorizontal;
        aimed.aimVFrom    = shown.vertical;
        aimed.aimVTo      = entity->aimVertical;
        aimed.viewBeginMs = now;
        aimed.viewEndMs   = now + Shared::SyncRate::kPlayerMs;

        aimed.position = entity->position;
        aimed.rotation = entity->rotation;
        aimed.speed    = glm::length(entity->velocity);
        aimed.whenMs   = now;
        aimed.valid    = true;
    }

    void ApplyRemoteState(Shared::PlayerEntity *entity) {
        const auto networkId = static_cast<std::uint64_t>(entity->GetNetworkID());

        bool      justCreated = false;
        const int slot        = AcquireRemotePedSlot(entity, networkId, justCreated);

        if (slot < 0) {
            return;
        }

        // Vehicle occupancy owns the ped while it is seated; when it reports
        // so, the position correction below is skipped deliberately.
        if (ApplyRemoteOccupancy(entity, slot, justCreated)) {
            return;
        }

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
        // Momentum, vitals and the transform are per-packet facts; input,
        // camera and pose are per-frame and follow below.
        if (changed || due) {
            ApplyRemoteSnapshot(entity, slot, aimed, now);
        }

        // 4. Input, AFTER the transform, so the game walks the ped from
        //    where it now is rather than from where it was last frame.
        //    Analog is lerped; discrete buttons still snap for edges.
        WriteLerpedRemotePad(slot, aimed, entity);

        // 5. The view, LERPED, not snapped. Camera rotation decides which
        //    way "forward" is, and the two aim angles decide where the
        //    hands point. All three used to be written straight from the
        //    last packet, ten times a second, which is invisible while
        //    walking and obvious the moment a gun comes up.
        aimed.gunUp = (entity->buttons & (SDK::Button::Aim | SDK::Button::Fire)) != 0;

        {
            const std::uint32_t nowView = GetTickCount();

            SDK::SetRemoteCameraRotation(slot, LerpRadians(aimed.cameraFrom, aimed.cameraTo, aimed.viewBeginMs, aimed.viewEndMs, nowView));

            SDK::SetRemotePedAim(slot, LerpRadians(aimed.aimHFrom, aimed.aimHTo, aimed.viewBeginMs, aimed.viewEndMs, nowView), LerpRadians(aimed.aimVFrom, aimed.aimVTo, aimed.viewBeginMs, aimed.viewEndMs, nowView));
        }

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
        }

        SDK::SetRemotePedInterior(slot, entity->interior);
    }
} // namespace VantaMP::Game::PlayerSyncState

namespace VantaMP::Game {
    using namespace PlayerSyncState;

    // Turns the ped a frame at a time toward the heading its owner reported.
    //
    // Frame by frame, NOT snapped ten times a second - a snapped heading shows
    // up first in the water, where a turn is all there is to see. This is what
    // MTA does (CClientPed::Interpolate).
    void InterpolateRemoteHeading(int slot) {
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

                    SDK::SetRemotePedHeading(slot, aimed.headingFrom + offset * (progress / span), !aimed.gunUp);
                } else {
                    // Landed exactly, rather than left wherever the last frame
                    // of the window happened to fall.
                    SDK::SetRemotePedHeading(slot, aimed.headingTo, !aimed.gunUp);

                    aimed.headingEndMs = 0;
                }
            }
        }
    }

    // Eases the ped toward where its owner says it is, or warps it when the gap
    // is absurd.
    //
    // A share of the error at a time is right for the metre or two a normal
    // update disagrees by; a warp is for the case where easing would take
    // visibly too long.
    void CorrectRemotePosition(int slot, Framework::Utils::Interpolator &interpolator) {
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


                SDK::SetRemotePedPosition(slot, target.x, target.y, target.z);

                // Aimed at itself, so the error left over from before the warp
                // is not paid off on top of a position that is already correct.
                interpolator.GetPosition()->SetTargetValue(target, target, static_cast<float>(Shared::SyncRate::kPlayerMs));

                return;
            }
        }

        const auto next = interpolator.GetPosition()->UpdateTargetValue(here);

        SDK::SetRemotePedPosition(slot, next.x, next.y, next.z);
    }

    void CorrectRemotePlayer(int slot) {
        if (const auto found = gRemoteAlpha.find(slot); found != gRemoteAlpha.end()) {
            SDK::SetRemotePedAlpha(slot, found->second);
        }

        if (SDK::IsPedOccupyingVehicle(slot)) {
            return;
        }

        // MID-FALL: GTA OWNS THIS PED, POSITION AND HEADING BOTH.
        //
        // CEventKnockOffBike applies a launch force and hands the ped to a
        // fall/get-up task. For that second or so they are tumbling under the
        // game's own physics, and every write from here fights it: the
        // position correction drags them back toward a target they are
        // deliberately flying away from, and the heading lerp above stands a
        // tumbling body upright.
        //
        // Standing off is safe because both machines run the SAME simulation
        // from the same starting state - the owner fell from the bike we have
        // synced all along. Any residual gap is paid off by the ordinary
        // correction once they are on their feet.
        if (SDK::PedIsFallingOffBike(slot)) {
            return;
        }

        const auto found = gInterpolationBySlot.find(slot);

        if (found == gInterpolationBySlot.end()) {
            return;
        }

        auto &interpolator = found->second;

        InterpolateRemoteHeading(slot);
        CorrectRemotePosition(slot, interpolator);
    }
} // namespace VantaMP::Game
