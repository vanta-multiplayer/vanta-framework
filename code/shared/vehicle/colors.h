#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace VantaMP::Shared {
    // Four body colours, packed 0x00RRGGBB. GTA only stores palette
    // indexes; these are the real RGB the client paints with.
    inline constexpr int kVehicleColorCount = 4;

    // Retail carcols.dat, 0–127. Used when a script still passes a
    // palette index (Vehicles.create's old two-byte args) so that path
    // becomes RGB rather than staying an index.
    inline constexpr std::uint32_t kVehiclePalette[128] = {
        0x000000, 0xF5F5F5, 0x2A77A1, 0x840410, 0x263739, 0x86446E, 0xD78E10, 0x4C75B7, 0xBDBEC6, 0x5E7072,
        0x46597A, 0x656A79, 0x5D7E8D, 0x58595A, 0xD6DAD6, 0x9CA1A3, 0x335F3F, 0x730E1A, 0x7B4C42, 0x9F9D94,
        0x3B4E78, 0x732E3E, 0x691E3B, 0x96918C, 0x515459, 0x3F3E45, 0xA5A9A7, 0x635C5A, 0x3D4A68, 0x979592,
        0x421F21, 0x5F272B, 0x8494AB, 0x767B7C, 0x646464, 0x5A5752, 0x252527, 0x2D3A35, 0x93A396, 0x6D7A88,
        0x221918, 0x6F675F, 0x7C1C2A, 0x5F0A15, 0x193826, 0x5D1B20, 0x9D9872, 0x7A7560, 0x989586, 0xADB0B0,
        0x848988, 0x304F45, 0x4D6268, 0x162248, 0x272F4B, 0x7D6256, 0x9EA4AB, 0x9C8D71, 0x6D1822, 0x4E6881,
        0x9C9C98, 0x917347, 0x661C26, 0x949D9F, 0xA4A7A5, 0x8E8C46, 0x341A1E, 0x6A7A8C, 0xAAAD8E, 0xAB2507,
        0x7D1B44, 0x400C1F, 0x36071C, 0x25250C, 0x223016, 0x7B701F, 0x05221A, 0x625544, 0x04E040, 0x360012,
        0xF2501A, 0xC12D4A, 0x132537, 0x96D1D0, 0x1A678F, 0x346382, 0x316C69, 0x3D3D3D, 0x6A4440, 0x0C202A,
        0xFC6B14, 0x2B1B1C, 0x1B1D21, 0x861A28, 0x8C8A87, 0xD30B0B, 0x320B0F, 0x155A0F, 0x06538D, 0x645B4F,
        0x166387, 0x19778F, 0x395A83, 0xA32831, 0x597D87, 0x22393F, 0x50636A, 0x2D3E40, 0x641B1E, 0x6D1A1C,
        0x402427, 0x835621, 0x7B3B2A, 0x154749, 0x4C6B87, 0x3F3E45, 0x11181C, 0x525252, 0x8A9899, 0x525C8C,
        0x7D8698, 0x212121, 0x3B3A3A, 0x6A7A8C, 0x152238, 0x50636A, 0x741E1E, 0x1A2B37,
    };

    [[nodiscard]] inline std::uint8_t RgbByte(float component) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(component, 0.0f, 1.0f) * 255.0f));
    }

    [[nodiscard]] inline std::uint32_t PackRgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        return (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
    }

    [[nodiscard]] inline std::uint8_t RgbR(std::uint32_t packed) {
        return static_cast<std::uint8_t>((packed >> 16) & 0xFFu);
    }

    [[nodiscard]] inline std::uint8_t RgbG(std::uint32_t packed) {
        return static_cast<std::uint8_t>((packed >> 8) & 0xFFu);
    }

    [[nodiscard]] inline std::uint8_t RgbB(std::uint32_t packed) {
        return static_cast<std::uint8_t>(packed & 0xFFu);
    }

    [[nodiscard]] inline std::uint32_t PaletteRgb(std::uint8_t index) {
        return kVehiclePalette[index < 128 ? index : 0];
    }

    // A handful of exact RGBs render as black on GTA materials. Nudge
    // each channel by one so the paint still lights.
    [[nodiscard]] inline std::uint32_t TweakVehicleRgb(std::uint32_t packed) {
        switch (packed & 0x00FFFFFFu) {
        case 0xFF00FF:
        case 0x00FFFF:
        case 0xFF00AF:
        case 0xFFAF00:
        case 0xB9FF00:
        case 0x00FFC8:
        case 0xFF3C00:
        case 0x3CFF00:
            return packed | 0x00010101u;
        default:
            return packed;
        }
    }
} // namespace VantaMP::Shared
