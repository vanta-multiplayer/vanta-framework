#include "game/sync/vehicle/common.h"

#include "sdk/vehicle/vehicle.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/sync_rates.h"

#include <utils/interpolator.h>

namespace VantaMP::Game::VehicleSync {
    void AimPosition(Frame &frame) {
        const auto current = SDK::ReadVehicle(frame.handle);

        // EXTRAPOLATE FROM THE VEHICLE'S OWN SPEED, NOT THE PACKET'S.
        //
        // MTA reads GetMoveSpeed() off the vehicle here, so the lead term
        // matches what the vehicle is doing right now rather than what it was
        // doing when the packet left. Using the packet's velocity against a
        // local position the game has since moved makes the aim point
        // disagree with the motion, and the error is re-aimed every packet -
        // which reads as stutter.
        constexpr float kFramesPerSecond = 50.0f;
        constexpr float kExtrapolateMs   = 5.0f;

        const glm::vec3 lead   = glm::vec3(current.vx, current.vy, current.vz) * (kFramesPerSecond * kExtrapolateMs / 1000.0f);
        const glm::vec3 target = frame.entity->position + lead;

        auto *position = frame.interpolator->GetPosition();

        position->SetErrorContributionDelayRange(100.0f, 400.0f);

        // 1.0, NOT 1.5. The factor clamps interpolation alpha
        // (interpolator.cpp: `alpha = clamp(alpha, 0, factor)`), so 1.5 lets
        // the correction run 50% PAST the target before it stops and the next
        // packet drags it back. MTA clamps at 1.0 and stops on the target.
        position->SetCompensationFactor(1.0f);
        position->SetTargetValue(glm::vec3(current.x, current.y, current.z), target, static_cast<float>(Shared::SyncRate::kVehicleMs));
    }

    void AimRotation(Frame &frame) {
        const auto current = SDK::ReadVehicle(frame.handle);

        auto *rotation = frame.interpolator->GetRotation();

        rotation->SetErrorContributionDelayRange(100.0f, 400.0f);
        rotation->SetTargetValue(current.rotation, frame.entity->rotation, static_cast<float>(Shared::SyncRate::kVehicleMs));
    }

    bool ShouldWarp(const Frame &frame) {
        if (!frame.aimed) {
            return false;
        }

        const auto      here = SDK::ReadVehicle(frame.handle);
        const glm::vec3 currentPos(here.x, here.y, here.z);

        const float threshold = Shared::Interpolation::VehicleWarpThresholdFor(frame.speed, Shared::SyncRate::kVehicleMs);

        return glm::distance(currentPos, frame.aimedPosition) > threshold;
    }

    void ReaimAfterWarp(Frame &frame) {
        const auto delay = static_cast<float>(Shared::SyncRate::kVehicleMs);

        frame.interpolator->GetPosition()->SetTargetValue(frame.aimedPosition, frame.aimedPosition, delay);
        frame.interpolator->GetRotation()->SetTargetValue(frame.aimedRotation, frame.aimedRotation, delay);
    }

    void ApplyMotion(Frame &frame, bool withMomentum) {
        // MOMENTUM IS A PER-PACKET FACT, NOT A PER-FRAME ONE.
        //
        // MTA writes SetMoveSpeed / SetTurnSpeed once, where the puresync
        // packet is read, then leaves the vehicle to GTA until the next
        // packet. Writing them every frame re-injects the same stale value
        // sixty times a second and cancels the damping GTA applies as part of
        // integrating a frame.
        //
        // The two paths are kept genuinely separate rather than sharing a
        // read-modify-write: ApplyVehicleMotion ALWAYS writes move and turn
        // speed, so reading the vehicle and writing it back would re-impose a
        // one-frame-old velocity on the frames that are supposed to leave the
        // vehicle alone. That is invisible on a car and is exactly what a
        // bike's suspension integrates against.
        if (withMomentum) {
            auto motion = SDK::ReadVehicle(frame.handle);

            motion.vx  = frame.velocity.x;
            motion.vy  = frame.velocity.y;
            motion.vz  = frame.velocity.z;
            motion.avx = frame.angular.x;
            motion.avy = frame.angular.y;
            motion.avz = frame.angular.z;

            motion.health   = frame.entity->health;
            motion.engineOn = frame.entity->engineOn;
            motion.sirenOn  = frame.entity->sirenOn;

            SDK::ApplyVehicleMotion(frame.handle, motion);
            return;
        }

        // States only. Nothing in GTA integrates these away, so they are safe
        // to hold every frame - and this leaves momentum untouched.
        SDK::ApplyVehicleStates(frame.handle, frame.entity->health, frame.entity->engineOn, frame.entity->sirenOn);
    }
} // namespace VantaMP::Game::VehicleSync
