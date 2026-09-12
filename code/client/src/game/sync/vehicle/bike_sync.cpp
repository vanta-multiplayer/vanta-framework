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
    } // namespace

    void OnSnapshot(Frame &frame) {
        // Position is still aimed for the driverless case, which uses the
        // smoothed path. A ridden bike drives XY itself by dead reckoning,
        // but aiming it costs nothing and keeps the state coherent for the
        // frame a rider steps off.
        AimPosition(frame);

        // ROTATION IS AIMED FROM THE LIVE MATRIX.
        //
        // This is what makes the wheelie path an interpolation rather than a
        // per-frame chase. The error between where GTA has the chassis RIGHT
        // NOW and where the owner says it is gets captured here, once, and is
        // then spent over exactly one packet window.
        //
        // Aiming it every packet also matters for the grounded case even
        // though nothing consumes it there: without it, the first frame of a
        // wheelie would inherit a stale error captured from whenever the
        // interpolator was last aimed, and jump.
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

            // ROLL, BUT ONLY WHEN THE BIKE IS NOT ON BOTH WHEELS.
            //
            // On two wheels, roll is suspension output and the network must
            // not touch it - that is the finding the whole policy rests on.
            //
            // A WHEELIE breaks that assumption. The front wheel lifts, and
            // GTA's roll stabilisation for exactly this case
            // (m_fWheelieStabMult / m_fStoppieStabMult in tBikeHandlingData)
            // is applied inside CBike::ProcessControl - which never runs for
            // a remote bike. So nothing stabilises the remote copy and
            // nothing corrects it either: it rolls away and lies over
            // sideways while the owner is balanced on the rear wheel.
            //
            // CBike::GetAllWheelsOffGround (Bike.cpp:532) is
            // `m_nNoOfContactWheels == 0`, and the count is what gates the
            // stabilisation, so it is the right thing to test. Below two
            // wheels of contact, roll goes back to being network
            // authoritative - eased, not snapped, so it does not fight
            // whatever contact response remains.
            //
            // Pitch stays GTA's throughout: that is the wheelie angle
            // itself, legitimately different on each machine.
            // WHEELIE DETECTION, THE WAY GTA ACTUALLY DOES IT.
            //
            // Two earlier gates were dead code because I invented the test
            // instead of reading it. GTA does not count wheels and does not
            // convert to an angle - CQuadBike::ProcessControl
            // (QuadBike.cpp:222, the reversed analogue of the bike code)
            // reads the FORWARD VECTOR'S Z directly:
            //
            //   if (AreFrontWheelsNotTouchingGround())
            //       if (... && m_matrix->GetForward().z > 0.0f)
            //           fabs(m_pHandling->m_fWheelieAng - GetForward().z)
            //
            // and m_fWheelieAng is stored as sin(angle)
            // (cHandlingDataMgr.cpp:359), i.e. the same space as forward.z,
            // so the two are directly comparable. Positive is nose-up.
            //
            // The OWNER'S forward.z is what we test, because the owner's
            // physics is the correct one; ours is the copy that has no
            // wheelie stabilisation (m_fWheelieStabMult lives in
            // CBike::ProcessControl, which never runs for a remote bike).
            //
            // Blend ramps in across the run up to the bike's own wheelie
            // angle, so entering and leaving does not step.
            const glm::mat3 wireBasis = glm::mat3_cast(glm::normalize(frame.entity->rotation));

            const float wireForwardZ = wireBasis[1].z;
            const float wheelieAng   = SDK::BikeWheelieAngle(frame.handle);

            // A small floor so a level bike's noise (forward.z ~ -0.05 in
            // normal riding) never trips this, and a sane fallback if the
            // handling data is missing.
            const float trigger = std::max(0.10f, wheelieAng * 0.35f);

            const bool wheelie = std::fabs(wireForwardZ) > trigger;

            if (wheelie) {
                // INTERPOLATED, NOT BLENDED PER FRAME.
                //
                // The previous attempt slerped the live matrix a fixed
                // fraction toward frame.entity->rotation every frame. That is
                // not interpolation, it is an exponential filter chasing a
                // target that is CONSTANT for the ~100ms until the next
                // packet - the same mistake the XY path already had and that
                // dead reckoning fixed there. It has two failure modes and
                // the log showed both: early in the packet window it moves
                // the chassis far too fast (0.35 of an 80-degree error in one
                // frame), and late in the window it is still pulling toward a
                // pose the owner left long ago.
                //
                // Framework's Interpolator is the right tool and is already
                // in the Frame. It captures the error ONCE when the packet
                // lands, spends it across exactly the packet window, and then
                // stops (interpolator.cpp:110 clears _finishTime at
                // alpha == factor). So the correction is finished, not
                // asymptotic, and the rate is derived from the tick rather
                // than from a constant I picked.
                //
                // Aimed in OnSnapshot from the bike's live matrix; consumed
                // here. UpdateTargetValue returns current * compensation, so
                // feeding it the live orientation each frame applies only
                // this frame's share of the error.
                const glm::quat next = frame.interpolator->GetRotation()->UpdateTargetValue(SDK::BikeOrientation(frame.handle));

                SDK::SetBikeOrientation(frame.handle, next);
            }
            else if (frame.packetArrived) {
                // GROUNDED: yaw only, on the packet edge. Pitch and roll are
                // left exactly as GTA resolved them - see the 6-7 degree
                // standing disagreement above, which is legitimate physics on
                // both machines and is the chassis hop if forced.
                SDK::SetBikeHeading(frame.handle, frame.entity->rotation);
            }
        }

        // AFTER any pose work, and only on the packet edge.
        ApplyMotion(frame, frame.packetArrived);

        // EVERY FRAME, because nothing else will do it. See point 3 above:
        // ProcessControlInputs is the only writer of m_fSteerAngle and the
        // only thing that clears the constructor's default handbrake, and it
        // does not run for a remote bike.
        SDK::ApplyBikeDriverOutputs(frame.handle, frame.entity->steer, frame.entity->handbrake, frame.entity->gasPedal, frame.entity->brakePedal);
    }
} // namespace VantaMP::Game::VehicleSync::Bike
