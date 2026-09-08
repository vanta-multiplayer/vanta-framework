#pragma once

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace VantaMP::Shared {
    // MTA vehicle rotation is degrees about X, Y, Z — not a heading, and
    // not GLM's pitch/yaw/roll constructor. The replica still stores a
    // quat; these two convert at the script edge so create / setRotation
    // and getRotation agree with MTA's CMatrix.
    //
    // Axes match GTA's CMatrix: column 0 right, 1 forward (GTA `up`),
    // 2 up (GTA `at`). That is the same basis SetVehiclePose writes.

    inline glm::quat QuatFromHeadingRadians(float heading) {
        return glm::quat(glm::vec3(0.0f, 0.0f, heading));
    }

    inline glm::quat QuatFromMtaDegrees(const glm::vec3 &degrees) {
        const glm::vec3 r = glm::radians(degrees);

        const float cosX = std::cos(-r.x);
        const float cosY = std::cos(-r.y);
        const float cosZ = std::cos(r.z);
        const float sinX = std::sin(-r.x);
        const float sinY = std::sin(-r.y);
        const float sinZ = std::sin(r.z);

        const glm::vec3 right(cosY * cosZ, cosY * sinZ, sinY);
        const glm::vec3 front(sinX * sinY * cosZ - cosX * sinZ, sinX * sinY * sinZ + cosX * cosZ, -sinX * cosY);
        const glm::vec3 up(-(cosX * sinY * cosZ + sinX * sinZ), cosZ * sinX - cosX * sinY * sinZ, cosX * cosY);

        return glm::normalize(glm::quat_cast(glm::mat3(right, front, up)));
    }

    inline glm::vec3 MtaDegreesFromQuat(const glm::quat &rotation) {
        const glm::mat3 axes  = glm::mat3_cast(glm::normalize(rotation));
        const glm::vec3 right = axes[0];
        const glm::vec3 front = axes[1];
        const glm::vec3 up    = axes[2];

        const float rotY = std::atan2(right.z, std::sqrt(right.x * right.x + right.y * right.y));
        const float rotZ = std::atan2(right.y, right.x);
        const float sinZ = -std::sin(rotZ);
        const float cosZ = std::cos(rotZ);
        const float rotX = std::atan2(up.x * sinZ + up.y * cosZ, front.x * sinZ + front.y * cosZ);

        return glm::degrees(glm::vec3(-rotX, -rotY, rotZ));
    }
} // namespace VantaMP::Shared
