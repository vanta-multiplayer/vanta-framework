#pragma once

#include <cstdint>

namespace VantaMP::Shared {
    // Two numbers that decide whether two entities share a world.
    //
    // Interior is GTA's area code (`CEntity::m_nAreaCode`, 0 = outside). The
    // local player's copy also lives in `CGame::currArea`; changing that is
    // what loads the interior's buildings. Dimension is not a GTA concept —
    // it is a virtual world index, the same idea as MTA's setElementDimension.
    // The interest grid is position-only, so isolation is applied on the
    // client: a replica in another dimension is not given a ped or a car.
    //
    // Same dimension AND same interior to interact. A player in a shop is
    // not visible, named or heard by someone standing on the street, even
    // when the coordinates overlap.

    inline constexpr std::uint8_t kWorldSpaceInterior  = 1;
    inline constexpr std::uint8_t kWorldSpaceDimension = 2;

    [[nodiscard]] inline bool SameDimension(std::uint16_t left, std::uint16_t right) {
        return left == right;
    }

    [[nodiscard]] inline bool SameWorld(std::uint8_t leftInterior, std::uint16_t leftDimension, std::uint8_t rightInterior, std::uint16_t rightDimension) {
        return leftDimension == rightDimension && leftInterior == rightInterior;
    }
} // namespace VantaMP::Shared
