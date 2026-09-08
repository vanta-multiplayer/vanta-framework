#pragma once

#include <cstdint>

namespace VantaMP::Shared {
    // GTA's thirteen weapon slots, from the type. The game has a table
    // for this; we keep a copy so the server can answer player.weaponSlot
    // without the exe.
    [[nodiscard]] inline std::uint8_t WeaponSlot(std::uint32_t type) {
        if (type <= 1) {
            return 0;
        }

        if (type <= 9 || type == 15) {
            return 1;
        }

        if (type <= 14) {
            return 10;
        }

        if (type <= 18 || type == 39) {
            return 8;
        }

        if (type <= 21) {
            return 8;
        }

        if (type <= 24) {
            return 2;
        }

        if (type <= 27) {
            return 3;
        }

        if (type == 28 || type == 29 || type == 32) {
            return 4;
        }

        if (type == 30 || type == 31) {
            return 5;
        }

        if (type <= 34) {
            return 6;
        }

        if (type <= 38) {
            return 7;
        }

        if (type == 40) {
            return 12;
        }

        if (type <= 43) {
            return 9;
        }

        if (type <= 45) {
            return 11;
        }

        if (type == 46) {
            return 12;
        }

        return 0;
    }
} // namespace VantaMP::Shared
