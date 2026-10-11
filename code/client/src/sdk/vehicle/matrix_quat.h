#pragma once

// GTA matrix <-> quaternion, shared by vehicle.cpp and bike.cpp.
//
// INTERNAL TO sdk/vehicle. Behind the SDK wall: it names CMatrix, so only
// files listed in VANTAMP_CLIENT_SDK_FILES may include it.
//
// GTA NAMES ITS AXES DIFFERENTLY FROM EVERYONE ELSE - `up` is forward and
// `at` is up. A quaternion built from the English names lands a car on its
// roof the first time it pitches, which is why this conversion exists in one
// place rather than being written out at each call site.

#include "sdk/game_sdk.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace VantaMP::SDK::Internal {
    // GTA names `up` as forward and `at` as up. A quat built from the
    // English names would land a car on its roof the first time it pitched.
    //
    // RE-ORTHONORMALISED FIRST. glm::quat_cast assumes a pure rotation,
    // and a GTA vehicle matrix is not reliably one: the game lets small
    // scale and shear accumulate (a bike's is rebuilt through the lean
    // matrix every frame, which is the worst case). Feeding that in
    // directly yields a quaternion that is not quite the orientation the
    // car has, and once that quat is interpolated and written back the
    // error compounds - the tilt the driver never had, drifting.
    //
    // Gram-Schmidt against forward and up, which is what the game's own
    // CMatrix::Reorthogonalise does.
    inline glm::quat QuatFromMatrix(const CMatrix &matrix) {
        const glm::vec3 forward(matrix.up.x, matrix.up.y, matrix.up.z);
        const glm::vec3 up(matrix.at.x, matrix.at.y, matrix.at.z);

        const float forwardLength = glm::length(forward);
        const float upLength      = glm::length(up);

        if (forwardLength < 1e-6f || upLength < 1e-6f) {
            return glm::identity<glm::quat>();
        }

        const glm::vec3 f = forward / forwardLength;
        glm::vec3       r = glm::cross(f, up / upLength);
        const float     rLength = glm::length(r);

        // Forward and up parallel: degenerate, nothing to build a basis
        // from. Keep what the car already has rather than inventing a roll.
        if (rLength < 1e-6f) {
            return glm::identity<glm::quat>();
        }

        r /= rLength;

        const glm::vec3 u = glm::cross(r, f);

        return glm::normalize(glm::quat_cast(glm::mat3(r, f, u)));
    }

    inline void ApplyQuatToMatrix(CMatrix &matrix, const glm::quat &rotation) {
        const glm::mat3 axes = glm::mat3_cast(glm::normalize(rotation));

        matrix.right = CVector(axes[0].x, axes[0].y, axes[0].z);
        matrix.up    = CVector(axes[1].x, axes[1].y, axes[1].z);
        matrix.at    = CVector(axes[2].x, axes[2].y, axes[2].z);
    }
} // namespace VantaMP::SDK::Internal
