#include "game/sync/vehicle/automobile_sync.h"

#include "game/sync/vehicle/common.h"
#include "sdk/vehicle/vehicle.h"
#include "shared/entity/vehicle_entity.h"

#include <utils/interpolator.h>

namespace VantaMP::Game::VehicleSync::Automobile {
    // THE TOLERANT CASE. This is the behaviour that already works, moved
    // here unchanged - do not "improve" it while chasing a bike bug.
    //
    // CAutomobile re-derives its four suspension points from the matrix
    // every frame in ProcessControl, so a transform written underneath it is
    // absorbed: the wheels simply re-resolve against the ground next tick.
    // That is what makes per-frame pose interpolation correct here and
    // wrong on two wheels.

    void OnSnapshot(Frame &frame) {
        AimPosition(frame);
        AimRotation(frame);
    }

    void Apply(Frame &frame) {
        if (ShouldWarp(frame)) {
            SDK::SetVehiclePose(frame.handle, frame.aimedPosition.x, frame.aimedPosition.y, frame.aimedPosition.z, frame.aimedRotation);
            ReaimAfterWarp(frame);
        }
        else {
            const auto here = SDK::ReadVehicle(frame.handle);

            const auto nextPos = frame.interpolator->GetPosition()->UpdateTargetValue(glm::vec3(here.x, here.y, here.z));
            const auto nextRot = frame.interpolator->GetRotation()->UpdateTargetValue(here.rotation);

            SDK::SetVehiclePose(frame.handle, nextPos.x, nextPos.y, nextPos.z, nextRot);
        }

        // AFTER THE POSE. SetPosn and collision from the correction can kill
        // the speed the game just integrated, so momentum goes back on the
        // frame a packet arrived - but only on that frame.
        ApplyMotion(frame, frame.packetArrived);

        // STEER IS AN OUTPUT FOR A CAR. ProcessControl rebuilds
        // m_fSteerAngle from the swapped pad every frame, so writing the last
        // packet's quantized angle over that is the cabin snap when they
        // turn. Pedals come from the same pad. Nothing to do here.
    }
} // namespace VantaMP::Game::VehicleSync::Automobile
