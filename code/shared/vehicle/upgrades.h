#pragma once

#include <cstdint>

namespace VantaMP::Shared {
    // MTA's 17 upgrade slots. GTA stores fewer on the vehicle; the extra
    // two (nitro / hydraulics / stereo live in the same table) are how
    // addVehicleUpgrade addressed every TransFender / Wheel Arch part.
    inline constexpr int kUpgradeSlots = 17;

    // Paintjob 0–2 are remaps; 3 is "none", which is what a fresh car has.
    inline constexpr std::uint8_t kPaintjobNone = 3;
    inline constexpr std::uint8_t kPaintjobMax  = 3;

    inline constexpr std::uint16_t kFirstUpgrade = 1000;
    inline constexpr std::uint16_t kLastUpgrade  = 1193;

    // Which slot an upgrade occupies, or -1 if the id is not a part.
    [[nodiscard]] int UpgradeSlot(std::uint16_t upgradeId);

    [[nodiscard]] inline bool IsPaintjob(std::uint8_t paintjob) {
        return paintjob <= kPaintjobMax;
    }

    // Every TransFender id that occupies that slot. Not filtered by
    // vehicle model — GTA's own shop list is in the exe.
    template <typename Fn>
    void ForEachUpgradeInSlot(int slot, Fn &&fn) {
        for (std::uint16_t id = kFirstUpgrade; id <= kLastUpgrade; ++id) {
            if (UpgradeSlot(id) == slot) {
                fn(id);
            }
        }
    }
} // namespace VantaMP::Shared
