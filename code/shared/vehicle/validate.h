#pragma once

#include <cmath>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace VantaMP::Shared {
    // SANITY BOUNDS FOR ANYTHING THAT ARRIVES OVER THE WIRE.
    //
    // MTA validates on every position READ as well as every write
    // (CEntitySA::GetPosition -> IsValidPosition -> RestoreLastGoodPhysicsState),
    // because one NaN that reaches a GTA matrix propagates: the entity's
    // transform, its collision, and anything that reads either all become
    // NaN, and nothing recovers on its own.
    //
    // The limits are MTA's (Client/game_sa/CEntitySA.h: IsValidPosition).
    inline constexpr float kWorldLimitXY = 16000.0f;
    inline constexpr float kWorldLimitZLow  = -5000.0f;
    inline constexpr float kWorldLimitZHigh = 100000.0f;

    [[nodiscard]] inline bool IsFiniteFloat(float value) {
        return std::isfinite(value);
    }

    [[nodiscard]] inline bool IsValidPosition(const glm::vec3 &p) {
        return IsFiniteFloat(p.x) && IsFiniteFloat(p.y) && IsFiniteFloat(p.z) && p.x > -kWorldLimitXY && p.x < kWorldLimitXY && p.y > -kWorldLimitXY
            && p.y < kWorldLimitXY && p.z > kWorldLimitZLow && p.z < kWorldLimitZHigh;
    }

    // A velocity that is not finite kills the entity the moment GTA
    // integrates it. The magnitude bound is deliberately generous - this is a
    // corruption guard, not a speed limit.
    [[nodiscard]] inline bool IsValidVelocity(const glm::vec3 &v) {
        constexpr float kMax = 1000.0f;

        return IsFiniteFloat(v.x) && IsFiniteFloat(v.y) && IsFiniteFloat(v.z) && std::fabs(v.x) < kMax && std::fabs(v.y) < kMax && std::fabs(v.z) < kMax;
    }

    // A quaternion has to be finite AND have a usable length, or normalising
    // it produces NaN and ApplyQuatToMatrix writes that straight into the
    // vehicle's orientation.
    [[nodiscard]] inline bool IsValidRotation(const glm::quat &q) {
        if (!IsFiniteFloat(q.x) || !IsFiniteFloat(q.y) || !IsFiniteFloat(q.z) || !IsFiniteFloat(q.w)) {
            return false;
        }

        const float lengthSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;

        return lengthSq > 1e-6f && lengthSq < 1e6f;
    }
} // namespace VantaMP::Shared
