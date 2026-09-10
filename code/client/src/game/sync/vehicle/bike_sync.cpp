#include "game/sync/vehicle/bike_sync.h"

#include "game/sync/vehicle/common.h"
#include "sdk/vehicle/vehicle.h"
#include "shared/entity/vehicle_entity.h"

#include <logging/logger.h>
#include <utils/interpolator.h>

#include <algorithm>
#include <cmath>

namespace VantaMP::Game::VehicleSync::Bike {
    // THE COMPLETE NETWORK SYNCHRONISATION LIFECYCLE OF A MOTORCYCLE.
    //
    // Three things make a CBike different from a CAutomobile, all of them
    // established from gta-reversed rather than guessed at. Everything in
    // this file follows from them.
    //
    // 1. A BIKE'S ORIENTATION IS PHYSICS, AND IT FIGHTS BACK.
    //
    //    Nudging the matrix every frame is a feedback loop on two wheels.
    //    Measured on an accelerating NRG-500 whose owner reported no
    //    rotation at all:
    //
    //      WIRE PACKET        ang( 0.0000, 0.0001, 0.0000)
    //      BEFORE GTA PROCTRL ts( 0.0000, 0.0001, 0.0000)   (ours)
    //      AFTER  GTA PROCTRL ts(-0.0012, 0.0007, 0.0023)   (GTA's)
    //      next frame         ts(-0.0037, 0.0017, 0.0124)   (growing)
    //
    //    We rotate the matrix a little; the suspension lines then sit wrong
    //    against the ground, so GTA raises real turn forces to correct what
    //    looks to it like a genuine attitude error, and CPhysical accumulates
    //    them into m_vecTurnSpeed (`m_vecTurnSpeed +=` in ApplyTurnForce and
    //    friction, Physical.cpp:740/767/2548). Next frame we rotate again and
    //    it compounds. A car absorbs those forces through four suspension
    //    points; a bike's free axis is ROLL, so it lies down while the owner
    //    rides upright.
    //
    // 2. A BIKE'S SUSPENSION STATE IS RELATIVE TO ITS OWN POSITION.
    //
    //      Bike.cpp:486      m_aGroundOffsets[i] = cp.m_vecPoint - entity->GetPosition()
    //      Physical.cpp:344  wheelsCollisionPositions[i] = wheelColPoint->m_vecPoint - GetPosition()
    //      Physical.cpp:320  wheel col-line ends via m_matrix->TransformPoint(...)
    //      Bike.cpp:419      ProcessEntityCollision compares this frame's wheel
    //                        ratios against last frame's (ogWheelRatios) and
    //                        skips a wheel whose contact did not improve
    //
    //    All of that assumes the chassis moved because PHYSICS moved it. Our
    //    frame order makes that false: CTheScripts::Process - where the sync
    //    tick runs - is Game.cpp:746, BEFORE CWorld::Process at 781. So a
    //    per-frame position write is followed immediately by GTA resolving
    //    two wheels against ground from a chassis that was teleported since
    //    it last measured. It pushes back along the suspension normal, we
    //    move it again next frame, and the chassis visibly bounces.
    //
    // 3. CBike::ProcessControlInputs NEVER RUNS FOR A REMOTE BIKE.
    //
    //    Measured with the pad demonstrably live and the owner at full lock:
    //
    //      PAD AT PROCESSCTRL     stickX128 ... RS1(handbrake)0
    //      AFTER GTA PROCESSCTRL  steer 0.00  hb1
    //      WIRE PACKET            steer60
    //
    //    m_fSteerAngle is assigned in exactly one place - inside
    //    ProcessControlInputs (Automobile.cpp:2037; CBike's copy is the
    //    unreversed 0x6BE310) - and that same function is the only thing that
    //    clears bIsHandbrakeOn, which CVehicle's constructor sets true
    //    (Vehicle.cpp:253). One missing call, two symptoms: the forks never
    //    turn and the handbrake never releases. So both are copied from the
    //    owner, who computed them in the only place GTA computes them.
    //
    // THE RESULTING POLICY, for a bike WITH a rider:
    //
    //   packet edge : place the authoritative pose once, write momentum once
    //   other frames: leave the chassis entirely alone
    //   every frame : copy steer and handbrake, which GTA will not derive
    //   warp        : place immediately, a discrete event suspension absorbs
    //
    // A DRIVERLESS bike is not being simulated as a driven vehicle - no
    // ProcessControlInputs, no lean, nothing to fight - so it takes the
    // ordinary smoothed path and looks better for it.

    namespace {
        // Between packets the chassis belongs to GTA. Only true while
        // somebody is actually riding it.
        [[nodiscard]] bool GtaOwnsChassis(const Frame &frame) {
            return frame.hasDriver;
        }

        [[nodiscard]] float YawOf(const glm::quat &q) {
            const glm::mat3 m = glm::mat3_cast(glm::normalize(q));

            // Forward is column 1 in our basis convention (GTA's m_forward).
            return std::atan2(m[1].x, m[1].y) * 57.2957795f;
        }

        [[nodiscard]] float PitchOf(const glm::quat &q) {
            return std::asin(std::clamp(glm::mat3_cast(glm::normalize(q))[1].z, -1.0f, 1.0f)) * 57.2957795f;
        }

        [[nodiscard]] float RollOf(const glm::quat &q) {
            return std::asin(std::clamp(-glm::mat3_cast(glm::normalize(q))[0].z, -1.0f, 1.0f)) * 57.2957795f;
        }

        [[nodiscard]] float AngleDelta(float a, float b) {
            float d = a - b;

            while (d > 180.0f) {
                d -= 360.0f;
            }

            while (d < -180.0f) {
                d += 360.0f;
            }

            return d;
        }

        // WHAT THE PACKET IS ACTUALLY ASKING US TO DO TO THE CHASSIS.
        //
        // Meaningful deltas rather than raw matrices: a large pitch or Z
        // correction here, correlated with a visible hop, is the evidence
        // that the network is overriding suspension state GTA had legitimately
        // resolved for itself.
        // IS THE REMOTE BIKE KEEPING UP, OR TRAILING?
        //
        // "Feels laggy" has two very different causes and this separates
        // them. Printed only while the bike is actually moving.
        //
        //   trail  = how far BEHIND the extrapolated target the bike sits,
        //            measured along its own direction of travel. A steady
        //            positive number is real positional lag.
        //   ageMs  = how old the packet being aimed at is. If this climbs
        //            past ~150ms the packets are late, not the correction.
        void LogTrail(const Frame &frame) {
            const auto here = SDK::ReadVehicle(frame.handle);

            const glm::vec2 gtaXY(here.x, here.y);
            const glm::vec2 wireVelXY(frame.entity->velocity.x, frame.entity->velocity.y);

            const float speed = glm::length(wireVelXY);

            if (speed < 0.02f) {
                return;
            }

            const std::uint32_t ageMs = GetTickCount() - frame.packetMs;
            const float         ageS  = std::clamp(static_cast<float>(ageMs), 0.0f, 250.0f) / 1000.0f;

            const glm::vec2 targetXY = glm::vec2(frame.entity->position.x, frame.entity->position.y) + wireVelXY * 50.0f * ageS;
            const glm::vec2 delta    = targetXY - gtaXY;

            // Positive = bike is behind where it should be.
            const float trail = glm::dot(delta, wireVelXY / speed);

            static std::uint32_t lastMs = 0;

            if (const std::uint32_t now = GetTickCount(); now - lastMs >= 250u) {
                lastMs = now;

                Framework::Logging::GetLogger("BikeDbg")
                    ->info("BIKE TRAIL  trail{: .3f}m lateral{: .3f}m | speed{:.2f} | packetAge{}ms | gtaSpeed{:.2f}", trail,
                           glm::length(delta - (wireVelXY / speed) * trail), speed * 50.0f, ageMs,
                           glm::length(glm::vec2(here.vx, here.vy)) * 50.0f);
            }
        }

        void LogPacketCorrection(const Frame &frame) {
            const auto here = SDK::ReadVehicle(frame.handle);

            const glm::vec3 gtaPos(here.x, here.y, here.z);
            const glm::vec3 wirePos = frame.entity->position;
            const glm::vec3 delta   = wirePos - gtaPos;

            Framework::Logging::GetLogger("BikeDbg")
                ->info("BIKE PACKET  gtaPos({:.2f},{:.2f},{:.3f}) wirePos({:.2f},{:.2f},{:.3f}) | dXY{:.3f} dZ{: .3f} | dYaw{: .2f} dPitch{: .2f} dRoll{: .2f} | contact{} gtaVz{: .3f}",
                       gtaPos.x, gtaPos.y, gtaPos.z, wirePos.x, wirePos.y, wirePos.z, glm::length(glm::vec2(delta.x, delta.y)), delta.z,
                       AngleDelta(YawOf(frame.entity->rotation), YawOf(here.rotation)),
                       AngleDelta(PitchOf(frame.entity->rotation), PitchOf(here.rotation)),
                       AngleDelta(RollOf(frame.entity->rotation), RollOf(here.rotation)), SDK::VehicleContactWheels(frame.handle), here.vz);
        }
    } // namespace

    void OnSnapshot(Frame &frame) {
        // Position is still aimed for the driverless case, which uses the
        // smoothed path. A ridden bike ignores the interpolator entirely, but
        // aiming it costs nothing and keeps the state coherent for the frame
        // a rider steps off.
        AimPosition(frame);
        AimRotation(frame);
    }

    // WHAT THE ISOLATION TESTS ESTABLISHED.
    //
    // TEST A ran with ZERO pose writes on a ridden bike. It settled two
    // things at once.
    //
    // (a) The hop is caused by the pose write. With writes off, the chassis
    //     is smooth - no hopping, no twitching, no pitch kick.
    //
    // (b) Pitch is NOT an error to be corrected. Riding with no corrections
    //     at all, the two machines sit at a PERSISTENT disagreement:
    //
    //       dPitch  6.03  7.25  7.40  7.10  7.30  6.51  6.30  6.08  5.79 ...
    //       dRoll  ~0.0 throughout
    //       dZ      0.002 - 0.009 throughout
    //
    //     Six to seven degrees, steady, never converging. That is each
    //     machine's own suspension and wheelie state under acceleration, and
    //     both values are locally correct. The old policy slammed that
    //     difference to zero ten times a second - which is precisely the
    //     visible hop, and why "one authoritative pose per packet" was wrong.
    //
    // (c) Z is never the problem. dZ stayed inside 1cm for the whole run,
    //     moving or stationary. There is nothing to reconcile vertically.
    //
    // (d) But XY drift is unbounded and fast. With no corrections:
    //
    //       dXY  0.03  0.17  0.39  0.44  0.69  0.87  1.29  1.37  1.49  2.03
    //
    //     Two metres in about a second of riding, still growing. Momentum
    //     alone does not keep a remote bike in place, so Test A is correct
    //     but not shippable.
    //
    // THE POLICY THAT FOLLOWS:
    //
    //   authoritative : X, Y  (navigation - where the bike is on the map)
    //                   yaw   (navigation - where the rider pointed it)
    //   GTA's own     : Z, pitch, roll, suspension, wheel contact, lean
    //
    // and the XY correction is eased in over the tick rather than snapped,
    // because a snap is a teleport and CBike's wheel-contact state is
    // relative to its own position (Bike.cpp:486, Physical.cpp:344).
    //
    // Yaw is corrected by rebuilding the basis around GTA's CURRENT Up
    // vector, so taking a heading cannot disturb pitch or roll - see
    // SDK::SetBikeHeading.

    void Apply(Frame &frame) {
        // A warp is a discrete event - the bike is somewhere it genuinely is
        // not supposed to be - and suspension re-measuring from the new spot
        // is correct.
        if (ShouldWarp(frame)) {
            SDK::SetVehiclePose(frame.handle, frame.aimedPosition.x, frame.aimedPosition.y, frame.aimedPosition.z, frame.aimedRotation);
            ReaimAfterWarp(frame);
        }
        else if (!GtaOwnsChassis(frame)) {
            // Driverless: nothing is simulating it as a driven vehicle, so
            // there is nothing to fight and the smoothed path looks better.
            const auto here = SDK::ReadVehicle(frame.handle);

            const auto nextPos = frame.interpolator->GetPosition()->UpdateTargetValue(glm::vec3(here.x, here.y, here.z));
            const auto nextRot = frame.interpolator->GetRotation()->UpdateTargetValue(here.rotation);

            SDK::SetVehiclePose(frame.handle, nextPos.x, nextPos.y, nextPos.z, nextRot);
        }
        else {
            // RIDDEN BIKE. Navigation is ours, physics is GTA's.
            //
            // Eased every frame rather than snapped on the packet edge: the
            // per-frame amount is a few millimetres, far below anything the
            // suspension reacts to, where a once-per-packet jump of the same
            // total distance is a teleport that invalidates wheel contact.
            // This is the opposite of the rotation case - a small continuous
            // XY nudge feeds no torque, which is why it is safe here and was
            // not safe for the basis.
            const auto here = SDK::ReadVehicle(frame.handle);

            const glm::vec2 gtaXY(here.x, here.y);

            // THE TARGET HAS TO MOVE BETWEEN PACKETS, OR THIS IS A BRAKE.
            //
            // frame.entity->position is the packet position and it is CONSTANT
            // for the ~100ms until the next one. Pulling the bike toward that
            // fixed point every frame drags it backwards against its own
            // forward motion for the whole interval, then the next packet
            // jumps the target forward - which is felt as lag even though the
            // bike is smooth.
            //
            // So the target is extrapolated by the owner's own velocity for
            // however long the packet has been in hand. That is dead
            // reckoning: between packets we aim at where the owner WOULD be,
            // not where they were.
            const float ageMs = static_cast<float>(GetTickCount() - frame.packetMs);
            const float ageS  = std::clamp(ageMs, 0.0f, 250.0f) / 1000.0f;

            const glm::vec2 wireVelXY(frame.entity->velocity.x, frame.entity->velocity.y);

            // Move speed is per-frame at 50fps in GTA's units, so scale it to
            // metres per second the same way the interpolator lead does.
            const glm::vec2 wireXY = glm::vec2(frame.entity->position.x, frame.entity->position.y) + wireVelXY * 50.0f * ageS;

            const glm::vec2 error = wireXY - gtaXY;

            // Ignore noise. Below this the bike is where it should be and
            // touching it only risks disturbing contact state.
            constexpr float kDeadzone = 0.02f;

            if (glm::length(error) > kDeadzone) {
                // Close the gap smoothly. Position only - Z comes from GTA,
                // which the data says is already correct.
                constexpr float kCatchUpPerFrame = 0.25f;

                const glm::vec2 step = error * kCatchUpPerFrame;

                SDK::SetBikeHorizontalPosition(frame.handle, gtaXY.x + step.x, gtaXY.y + step.y);
            }

            LogTrail(frame);

            if (frame.packetArrived) {

                // Yaw only, on the packet edge. Pitch and roll are left
                // exactly as GTA resolved them - see the 6-7 degree standing
                // disagreement above, which is legitimate physics on both
                // machines and must not be reconciled.
                SDK::SetBikeHeading(frame.handle, frame.entity->rotation);
            }
        }

        // AFTER any pose work, and only on the packet edge.
        ApplyMotion(frame, frame.packetArrived);

        // EVERY FRAME, because nothing else will do it. See point 3 above:
        // ProcessControlInputs is the only writer of m_fSteerAngle and the
        // only thing that clears the constructor's default handbrake, and it
        // does not run for a remote bike.
        SDK::ApplyBikeDriverOutputs(frame.handle, frame.entity->steer, frame.entity->handbrake);
    }
} // namespace VantaMP::Game::VehicleSync::Bike
