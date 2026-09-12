#include "vehicle.h"

#include "sdk/game_sdk.h"
#include "sdk/vehicle/matrix_quat.h"
#include "sdk/patterns.h"
#include "sdk/ped/access.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>

#include <CBike.h>

#include <algorithm>
#include <cmath>

// CBIKE, WHICH IS NOT A CAR WITH TWO FEWER WHEELS.
//
// A bike leans, and the lean is PHYSICS, not presentation: GTA generates a
// roll force from steer input and integrates it into m_vecTurnSpeed. So the
// network must not write roll while the bike is upright on two wheels - that
// fights the suspension and is what made remote bikes wobble and hop.
//
// These wrappers exist so the sync layer can express "set the heading but
// leave the lean alone", which no generic vehicle call can say.
//
// Split out of vehicle.cpp, which had grown to hold the generic vehicle
// wrappers and this whole cluster at once.
namespace VantaMP::SDK {
    using Internal::ApplyQuatToMatrix;
    using Internal::QuatFromMatrix;

    namespace {
        // GTA calls this file reaches through. Duplicated from vehicle.cpp
        // rather than promoted to a shared header: they are one line each and
        // exist only because plugin-sdk cannot name these functions.
        using SetPosnFn        = void(__thiscall *)(void *, float, float, float);
        using UpdateRwMatrixFn = void(__thiscall *)(void *);
        using UpdateRwFrameFn  = void(__thiscall *)(void *);
        using WorldRemoveFn    = void(__cdecl *)(void *);
        using WorldAddFn       = void(__cdecl *)(void *);

        // m_fSteerAngle is radians; the wire carries an int8. 1/128 is the
        // scale that round-trips the pad's own range.
        constexpr float kSteerScale = 1.0f / 128.0f;

        CVehicle *AsVehicle(void *vehicle) {
            return static_cast<CVehicle *>(vehicle);
        }

        const CVehicle *AsVehicle(const void *vehicle) {
            return static_cast<const CVehicle *>(vehicle);
        }

        // Local copy: the generic file has its own, and neither is worth
        // promoting to a shared header for two lines.
        CMatrix *MatrixOf(CVehicle *vehicle) {
            return vehicle && vehicle->m_matrix ? vehicle->m_matrix : nullptr;
        }

        const CMatrix *MatrixOf(const CVehicle *vehicle) {
            return vehicle && vehicle->m_matrix ? vehicle->m_matrix : nullptr;
        }
    } // namespace

    // A REMOTE BIKE NEVER RUNS ProcessControlInputs, SO ITS OUTPUTS MUST BE
    // COPIED FROM THE WIRE.
    //
    // For a car the steer angle really is an output: GTA rebuilds it from
    // GetPad(0) every frame, which is why writing it used to be pointless.
    // A ridden remote BIKE does not get that far. Runtime trace, pad clearly
    // live and the owner clearly steering:
    //
    //   PAD AT PROCESSCTRL  stickX128 ... RS1(handbrake)0 triangle(exit)0
    //   AFTER GTA PROCESSCTRL  steer 0.00  hb1
    //   WIRE PACKET            steer60
    //
    // m_fSteerAngle is assigned in exactly one place - inside
    // ProcessControlInputs (CAutomobile.cpp:2037,
    // `m_fSteerAngle = SteeringLock * signedSquare(m_fRawSteerAngle)`), and
    // CBike's copy of that function is 0x6BE310. It plainly is not running
    // for this vehicle: the stick is at full lock and the angle stays 0.00.
    // The same function is the only thing that clears bIsHandbrakeOn, which
    // CVehicle's constructor sets true (Vehicle.cpp:253) - which is why the
    // handbrake never lets go either. One missing call, both symptoms.
    //
    // So the values it would have produced are taken from the owner, who
    // computed them in the only place GTA computes them - their own
    // ProcessControlInputs - and already sends them. Steer drives the front
    // fork and wheel visuals; the handbrake flag gates braking behaviour.
    //
    // THE PEDALS BELONG IN HERE TOO, AND USED NOT TO BE.
    //
    // The old comment said "pedals are left to the pad, which does reach the
    // engine". That is the same mistake steer was: the pad reaches the
    // engine, but it does NOT reach m_fGasPedal, because the only function
    // that converts pad buttons into that field is the one established above
    // as never running. Measured with reverse plainly held on the owner:
    //
    //   PAD AT PROCESSCTRL  ... cross0 square255 ...   (reverse, arriving)
    //   m_fGasPedal         0.00                       (never written)
    //
    // The rider's animation is selected from the pedal, so a remote rider
    // stayed in the idle pose while reversing instead of looking back over
    // their shoulder.
    void ApplyBikeDriverOutputs(void *handle, std::int8_t steer, bool handbrake, std::int8_t gasPedal, std::int8_t brakePedal) {
        auto *vehicle = AsVehicle(handle);

        // ONLY WHILE SOMEBODY IS ACTUALLY RIDING IT.
        //
        // A driverless bike is GTA's business: it wants the handbrake ON (the
        // parked branch of ProcessControl sets it, and the constructor
        // defaults it true), and clearing it from here just fought the game
        // every frame - hb went 1 -> 0 -> 1 sixty times a second on a bike
        // nobody was sitting on, which is churn we do not want and had no
        // authoritative brake state behind it anyway.
        if (!vehicle || IsLocalPlayerDriving(vehicle) || !IsBikeHandle(vehicle) || !vehicle->m_pDriver) {
            return;
        }

        vehicle->m_fSteerAngle = -static_cast<float>(steer) * kSteerScale;

        // Straight from the owner's own bIsHandbrakeOn. It was WRONG to
        // derive this from `brake`: that field is m_fBreakPedal, the foot
        // brake, so ordinary braking would have engaged the handbrake on the
        // remote copy - a bug this fix would have introduced.
        vehicle->bIsHandbrakeOn = handbrake;

        // Signed, so reverse survives. kSignedPedalScale is the inverse of
        // the 127 the sender scaled by.
        constexpr float kSignedPedalScale = 1.0f / 127.0f;

        vehicle->m_fGasPedal   = static_cast<float>(gasPedal) * kSignedPedalScale;
        vehicle->m_fBreakPedal = static_cast<float>(brakePedal) * kSignedPedalScale;
    }

    // HORIZONTAL POSITION ONLY, LEAVING Z AND THE WHOLE BASIS ALONE.
    //
    // For a grounded ridden bike, Z and the pitch/roll of the basis are
    // suspension output - GTA resolved them against the road this frame and
    // they are more correct than a ~100ms-old snapshot from another machine.
    // This corrects where the bike is on the map without touching how it is
    // sitting on its wheels.
    void SetBikeHorizontalPosition(void *handle, float x, float y) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || !vehicle->m_matrix) {
            return;
        }

        const float z = vehicle->m_matrix->pos.z;

        reinterpret_cast<WorldRemoveFn>(gPatterns.CWorld__Remove)(vehicle);
        reinterpret_cast<SetPosnFn>(gPatterns.CPlaceable__SetPosn)(vehicle, x, y, z);
        reinterpret_cast<UpdateRwMatrixFn>(gPatterns.CEntity__UpdateRwMatrix)(vehicle);
        reinterpret_cast<UpdateRwFrameFn>(gPatterns.CEntity__UpdateRwFrame)(vehicle);
        reinterpret_cast<WorldAddFn>(gPatterns.CWorld__Add)(vehicle);
    }

    // HEADING ONLY, PRESERVING GTA'S PITCH AND ROLL.
    //
    // Yaw is navigation - where the rider pointed the bike - and is
    // legitimately network-authoritative. Pitch and roll are suspension and
    // lean, which this machine's physics resolved for itself. Rebuilding the
    // basis around the current Up vector keeps those intact while turning the
    // bike to the heading the owner reports.
    void SetBikeHeading(void *handle, const glm::quat &rotation) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || !vehicle->m_matrix) {
            return;
        }

        auto &matrix = *vehicle->m_matrix;

        const glm::vec3 up(matrix.at.x, matrix.at.y, matrix.at.z);
        const glm::vec3 wantedForward = glm::mat3_cast(glm::normalize(rotation))[1];

        // Project the wanted heading onto the plane GTA's own Up defines, so
        // only the yaw component of the packet is taken.
        glm::vec3 forward = wantedForward - up * glm::dot(wantedForward, up);

        if (glm::length(forward) < 1e-4f) {
            return;
        }

        forward = glm::normalize(forward);

        const glm::vec3 right = glm::normalize(glm::cross(forward, up));

        matrix.right = CVector(right.x, right.y, right.z);
        matrix.up    = CVector(forward.x, forward.y, forward.z);
        // matrix.at (GTA's up) deliberately untouched.

        reinterpret_cast<UpdateRwMatrixFn>(gPatterns.CEntity__UpdateRwMatrix)(vehicle);
        reinterpret_cast<UpdateRwFrameFn>(gPatterns.CEntity__UpdateRwFrame)(vehicle);
    }

    // ROLL, EASED TOWARD THE OWNER'S, KEEPING GTA'S PITCH AND HEADING.
    //
    // For a bike ON BOTH WHEELS, roll is suspension output and the network
    // must not touch it - that was the whole finding behind the current
    // policy. But during a WHEELIE the front wheel leaves the ground and
    // GTA's own roll stabilisation (m_fWheelieStabMult, tBikeHandlingData)
    // lives inside CBike::ProcessControl - which never runs for a remote
    // bike. With nothing stabilising it and nothing correcting it, the
    // remote copy is free to roll away and lie over sideways while the owner
    // is balanced on the rear wheel.
    //
    // So when contact is reduced, roll becomes network-authoritative again,
    // eased rather than snapped so it does not fight what little suspension
    // response remains. Pitch stays GTA's - that is the wheelie angle
    // itself, and it is legitimately different on each machine.
    void BlendBikeRoll(void *handle, const glm::quat &rotation, float alpha) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || !vehicle->m_matrix) {
            return;
        }

        auto &matrix = *vehicle->m_matrix;

        const glm::vec3 gtaForward(matrix.up.x, matrix.up.y, matrix.up.z);
        const glm::vec3 gtaUp(matrix.at.x, matrix.at.y, matrix.at.z);

        const glm::mat3 wire      = glm::mat3_cast(glm::normalize(rotation));
        const glm::vec3 wantedUp  = wire[2];

        // Only the component of the owner's Up that is perpendicular to our
        // own forward axis is roll; the rest is pitch, which stays ours.
        glm::vec3 targetUp = wantedUp - gtaForward * glm::dot(wantedUp, gtaForward);

        if (glm::length(targetUp) < 1e-4f) {
            return;
        }

        targetUp = glm::normalize(targetUp);

        glm::vec3 blended = glm::normalize(glm::mix(gtaUp, targetUp, glm::clamp(alpha, 0.0f, 1.0f)));

        // Rebuild an orthonormal basis around our forward and the blended up.
        glm::vec3 right = glm::cross(gtaForward, blended);

        if (glm::length(right) < 1e-4f) {
            return;
        }

        right = glm::normalize(right);

        const glm::vec3 up = glm::normalize(glm::cross(right, gtaForward));

        matrix.right = CVector(right.x, right.y, right.z);
        matrix.up    = CVector(gtaForward.x, gtaForward.y, gtaForward.z);
        matrix.at    = CVector(up.x, up.y, up.z);

        reinterpret_cast<UpdateRwMatrixFn>(gPatterns.CEntity__UpdateRwMatrix)(vehicle);
        reinterpret_cast<UpdateRwFrameFn>(gPatterns.CEntity__UpdateRwFrame)(vehicle);
    }

    // THE BIKE'S CURRENT ORIENTATION, AS A QUATERNION.
    //
    // The read half of the wheelie path. The interpolator needs a "current"
    // to aim FROM when a packet lands, and it has to be the live matrix
    // rather than anything cached, because GTA has integrated the chassis
    // since the last packet.
    glm::quat BikeOrientation(const void *handle) {
        auto *vehicle = AsVehicle(const_cast<void *>(handle));

        if (!vehicle || !vehicle->m_matrix) {
            return glm::identity<glm::quat>();
        }

        return QuatFromMatrix(*vehicle->m_matrix);
    }

    // WRITES AN ORIENTATION THE CALLER HAS ALREADY DECIDED ON.
    //
    // No blending of its own - deliberately. The wheelie path drives this
    // from Framework's Interpolator, which consumes a fixed error across the
    // packet window and stops when it is spent. Easing again in here would
    // put a second, unsynchronised filter in series with that one and the
    // pair would never converge.
    void SetBikeOrientation(void *handle, const glm::quat &rotation) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || !vehicle->m_matrix) {
            return;
        }

        ApplyQuatToMatrix(*vehicle->m_matrix, glm::normalize(rotation));

        reinterpret_cast<UpdateRwMatrixFn>(gPatterns.CEntity__UpdateRwMatrix)(vehicle);
        reinterpret_cast<UpdateRwFrameFn>(gPatterns.CEntity__UpdateRwFrame)(vehicle);
    }

    // EASES THE WHOLE BASIS TOWARD THE OWNER'S, KEEPING POSITION.
    //
    // For a GROUNDED bike this would be wrong - pitch and roll are that
    // machine's own suspension output and forcing them is the chassis hop.
    // During a WHEELIE it is the only correct thing: the remote bike is not
    // doing the wheelie at all (measured forward.z ~0.1 against the owner's
    // ~0.99, an 80-degree pitch disagreement), because the wheelie is
    // produced by CBike::ProcessControl, which never runs for a remote bike.
    //
    // Eased rather than snapped so the entry and exit do not step.
    void BlendBikeOrientation(void *handle, const glm::quat &rotation, float alpha) {
        auto *vehicle = AsVehicle(handle);

        if (!vehicle || !vehicle->m_matrix) {
            return;
        }

        auto &matrix = *vehicle->m_matrix;

        const glm::quat current = QuatFromMatrix(matrix);
        const glm::quat blended = glm::slerp(current, glm::normalize(rotation), glm::clamp(alpha, 0.0f, 1.0f));

        ApplyQuatToMatrix(matrix, blended);

        reinterpret_cast<UpdateRwMatrixFn>(gPatterns.CEntity__UpdateRwMatrix)(vehicle);
        reinterpret_cast<UpdateRwFrameFn>(gPatterns.CEntity__UpdateRwFrame)(vehicle);
    }

    // GTA'S OWN WHEELIE MEASURE: THE FORWARD VECTOR'S Z COMPONENT.
    //
    // From CQuadBike::ProcessControl (QuadBike.cpp:222), which is the
    // reversed analogue of the bike code:
    //
    //   if (AreFrontWheelsNotTouchingGround())
    //       if (... && m_matrix->GetForward().z > 0.0f)
    //           ... fabs(m_pHandling->m_fWheelieAng - m_matrix->GetForward().z) ...
    //   else if (... m_matrix->GetForward().z < 0.0f)
    //           ... fabs(m_pHandling->m_fStoppieAng - m_matrix->GetForward().z) ...
    //
    // So the game does NOT count wheels or convert to an angle. It reads
    // forward.z directly and compares it against m_fWheelieAng, which
    // cHandlingDataMgr stores as sin(angle) (cHandlingDataMgr.cpp:359,
    // `m_fWheelieAng = sin(DegreesToRadians(m_fWheelieAng))`) - the same
    // space as forward.z. Positive is nose-up, negative nose-down.
    //
    // Returns forward.z, so 0 is level, positive a wheelie, negative a
    // stoppie. Callers compare against BikeWheelieAngle() below.
    float BikeForwardZ(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        if (!vehicle || !vehicle->m_matrix) {
            return 0.0f;
        }

        // GTA's m_forward is plugin-sdk's `up`.
        return vehicle->m_matrix->up.z;
    }

    // m_fWheelieAng for this bike, in the same sin() space as forward.z.
    // The angle at which GTA considers the bike to be in a full wheelie.
    float BikeWheelieAngle(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        if (!vehicle || !IsBikeHandle(vehicle)) {
            return 0.0f;
        }

        const auto *bike = static_cast<const CBike *>(vehicle);

        if (!bike->m_pBikeHandlingData) {
            return 0.0f;
        }

        return bike->m_pBikeHandlingData->m_fWheelieAng;
    }

    // IS THE BIKE OFF ITS SUSPENSION - WHEELIE, STOPPIE OR AIRBORNE?
    //
    // gta-reversed spells out the test (Bike.h):
    //
    //   IsAnyWheelNotMakingContactWithGround()
    //       any_of(m_aWheelRatios, [](r) { return r == 1.0f; })
    //
    // A ratio of exactly 1.0 is a fully extended suspension line, i.e. that
    // line found no ground. This is the honest signal; the contact COUNT is
    // over four lines and stays at 4 far longer than the bike is level.
    //
    // Returns how many of the four lines are off the ground: 0 grounded,
    // 4 fully airborne, in between a wheelie or stoppie.
    int BikeWheelsOffGround(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        if (!vehicle || !IsBikeHandle(vehicle)) {
            return -1;
        }

        const auto *bike = static_cast<const CBike *>(vehicle);

        int off = 0;

        for (const float ratio : bike->m_aWheelRatios) {
            if (ratio >= 1.0f) {
                ++off;
            }
        }

        return off;
    }

    bool IsBikeHandle(const void *handle) {
        const auto *vehicle = AsVehicle(handle);

        return vehicle && (vehicle->m_nVehicleSubClass == VEHICLE_BIKE || vehicle->m_nVehicleSubClass == VEHICLE_BMX);
    }
} // namespace VantaMP::SDK
