#pragma once

#include <algorithm>
#include <cstdint>

namespace VantaMP::Shared {
    // How often each kind of state goes on the wire.
    //
    // NOT EVERY FRAME. A player's position does not need sending sixty times a
    // second: the receiving client interpolates between updates and the game
    // animates from the input that came with them, so a higher rate costs
    // bandwidth for movement nobody can see. MTA settled on these numbers over
    // twenty years of running actual servers, and they are a better starting
    // point than anything derived from first principles here.
    namespace SyncRate {
        // Player position, rotation and input. MTA calls this "pure sync".
        inline constexpr std::uint32_t kPlayerMs = 100;

        // Occupied (and last-driver empty) vehicle motion. Same cadence as
        // the driver, because the car is what everyone else is watching.
        inline constexpr std::uint32_t kVehicleMs = 100;

        // Players far enough away that their exact pose does not matter. MTA
        // drops to this to keep a busy server's bandwidth bounded.
        inline constexpr std::uint32_t kDistantPlayerMs = 1500;

        // How long a received input keeps driving a ped before it is treated as
        // stale.
        //
        // THREE INTERVALS, WITH A FLOOR. A remote player has to keep walking
        // through a missed update or two, or every dropped packet is a visible
        // stutter - but held for ever, a player whose client stopped talking
        // walks into the distance for good. MTA uses the same rule.
        [[nodiscard]] inline constexpr std::uint32_t InputLeaseMs(std::uint32_t syncIntervalMs) {
            return std::max<std::uint32_t>(500, syncIntervalMs * 3);
        }
    } // namespace SyncRate

    // Beyond this the receiver stops interpolating and simply puts the ped
    // where it was told.
    //
    // SCALED BY SPEED, because a player sprinting covers more ground between
    // two updates than one walking, and a fixed threshold either snaps a
    // sprinter constantly or lets a walker slide visibly. MTA's numbers.
    namespace Interpolation {
        inline constexpr float kWarpThreshold         = 5.0f;
        inline constexpr float kWarpThresholdPerSpeed = 5.0f;

        [[nodiscard]] inline float WarpThresholdFor(float speed, std::uint32_t syncIntervalMs) {
            return (kWarpThreshold + kWarpThresholdPerSpeed * speed) * (static_cast<float>(syncIntervalMs) / 100.0f);
        }

        // MTA CClientVehicle: 15 + 10 * speed, then * tick/100.
        // Tighter numbers snap a moving car every packet, which is the
        // stutter - and a car that warps while someone is punching it
        // launches them.
        inline constexpr float kVehicleWarpThreshold         = 15.0f;
        inline constexpr float kVehicleWarpThresholdPerSpeed = 10.0f;

        [[nodiscard]] inline float VehicleWarpThresholdFor(float speed, std::uint32_t syncIntervalMs) {
            return (kVehicleWarpThreshold + kVehicleWarpThresholdPerSpeed * speed) * (static_cast<float>(syncIntervalMs) / 100.0f);
        }
    } // namespace Interpolation
} // namespace VantaMP::Shared
