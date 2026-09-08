#pragma once

#include "shared/vehicle/colors.h"
#include "shared/vehicle/upgrades.h"

#include <networking/replication/network_entity.h>

#include <array>
#include <cstdint>
#include <string>

namespace VantaMP::Shared {
    // A vehicle, as everyone else sees it.
    //
    // Same replica shape as PlayerEntity: one definition, both sides, append
    // only. Transform (position, velocity, full-orientation quat) lives on
    // NetworkEntity. A car is not a heading - it pitches, rolls and can sit
    // on its roof - so rotation here is the matrix, not yaw.
    //
    // Authority is ownerGUID. The driver owns the car while they sit in it
    // and after they get out (so an empty car still rolls). Disconnect
    // returns ownership to the server, which then holds the last pose.
    class VehicleEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "VantaMPVehicle";

        static constexpr std::uint16_t kFirstModel = 400;
        static constexpr std::uint16_t kLastModel  = 611;
        static constexpr std::uint8_t  kMaxSeat    = 8;
        static constexpr std::uint8_t  kDoorCount  = 6;
        static constexpr std::uint8_t  kWheelCount = 4;
        static constexpr std::uint8_t  kPanelCount  = 7;
        static constexpr std::uint8_t  kLightCount  = 4;
        static constexpr float         kWreckHealth = 250.0f;

        // Worse-wins per GTA field, not the packed integer. panels is seven
        // 4-bit states; a numeric > compared the windscreen to the bumper.
        static std::uint64_t WorseBytes(std::uint64_t left, std::uint64_t right, int count) {
            std::uint64_t worse = 0;

            for (int i = 0; i < count; ++i) {
                const auto shift = static_cast<std::uint64_t>(i * 8);
                const auto a     = static_cast<std::uint8_t>((left >> shift) & 0xFFu);
                const auto b     = static_cast<std::uint8_t>((right >> shift) & 0xFFu);

                worse |= static_cast<std::uint64_t>(a > b ? a : b) << shift;
            }

            return worse;
        }

        static std::uint32_t WorsePacked(std::uint32_t left, std::uint32_t right, int count, int bits) {
            std::uint32_t       worse = 0;
            const std::uint32_t mask  = (1u << bits) - 1u;

            for (int i = 0; i < count; ++i) {
                const auto shift = i * bits;
                const auto a     = (left >> shift) & mask;
                const auto b     = (right >> shift) & mask;

                worse |= (a > b ? a : b) << shift;
            }

            return worse;
        }

        // --- Construction ------------------------------------------------------

        std::uint32_t modelId         = 0;
        std::uint8_t  primaryColour   = 0;
        std::uint8_t  secondaryColour = 0;

        // --- Per-tick ----------------------------------------------------------

        glm::vec3 angularVelocity {};

        float health = 1000.0f;

        bool engineOn = true;
        bool sirenOn  = false;

        // Latch, not health. GTA blows around 250; the server has to
        // remember the wreck after the last driver leaves or a late
        // joiner gets a fresh CreateCarForScript at health 0.
        bool blown = false;

        // Driver pad, not wheel angles. ProcessControl recomputes steering
        // from whoever it believes is driving; a remote car has no local
        // driver, so these have to be written back every frame.
        std::int8_t  steer       = 0;
        std::uint8_t accelerator = 0;
        std::uint8_t brake       = 0;

        // Packed the way GTA stores them. A late joiner must see a wreck,
        // not wait for a change that already happened.
        std::uint8_t  damageEngine     = 0;
        std::uint64_t doorsPacked      = 0;
        std::uint32_t wheelsPacked     = 0;
        std::uint32_t panels           = 0;
        std::uint32_t lights           = 0;
        // Six door open ratios, 0-255 each. Status bytes say what is
        // broken; these say how far the hood, boot and doors are swung.
        // A popped hood is almost always this, not a damage flag.
        std::uint64_t doorRatiosPacked = 0;

        // Same pair as PlayerEntity. An empty car is server-owned, so a
        // script write persists until a driver claims it. The driver then
        // echoes dimension and reports interior from the GTA vehicle.
        std::uint8_t  interior  = 0;
        std::uint16_t dimension = 0;

        // Cosmetics. Server / script write these; the owner echoes and
        // never reads them back from GTA. A client that "found" a nitro
        // in a cheat menu does not get to put it on the wire.
        std::uint8_t paintjob = kPaintjobNone;
        std::array<std::uint16_t, kUpgradeSlots> upgrades {};

        // Four body colours, 0x00RRGGBB. Not palette indexes — GTA only
        // has 128 of those, and a script can ask for any RGB. The two
        // construction bytes stay as a late-join fallback.
        std::array<std::uint32_t, kVehicleColorCount> colours {};

        // Script lights / lock. Owner never reads these back from GTA.
        // overrideLights: 0 none, 1 force off, 2 force on — GTA's own field.
        std::uint8_t overrideLights = 0;
        bool         locked         = false;

        // More script cosmetics / switches. Owner echoes, never reads GTA.
        std::string   plateText;
        std::uint8_t  dirtLevel      = 0;
        bool          taxiLight      = false;
        std::uint32_t headlightColor = 0;
        bool          frozen         = false;
        bool          damageProof    = false;

        static std::uint8_t PackedByte(std::uint64_t packed, int index) {
            return static_cast<std::uint8_t>((packed >> (index * 8)) & 0xFFu);
        }

        static std::uint64_t SetPackedByte(std::uint64_t packed, int index, std::uint8_t value) {
            const auto shift = index * 8;

            packed &= ~(0xFFull << shift);
            packed |= static_cast<std::uint64_t>(value) << shift;
            return packed;
        }

        static std::uint32_t PackedBits(std::uint32_t packed, int index, int bits) {
            const auto mask = (1u << bits) - 1u;

            return (packed >> (index * bits)) & mask;
        }

        static std::uint32_t SetPackedBits(std::uint32_t packed, int index, int bits, std::uint32_t value) {
            const auto mask  = (1u << bits) - 1u;
            const auto shift = index * bits;

            packed &= ~(mask << shift);
            packed |= (value & mask) << shift;
            return packed;
        }

        void SetColours(std::uint32_t c1, std::uint32_t c2, std::uint32_t c3, std::uint32_t c4) {
            colours[0] = TweakVehicleRgb(c1);
            colours[1] = TweakVehicleRgb(c2);
            colours[2] = TweakVehicleRgb(c3);
            colours[3] = TweakVehicleRgb(c4);
        }

        void FillColoursFromPalette() {
            const auto primary   = PaletteRgb(primaryColour);
            const auto secondary = PaletteRgb(secondaryColour);

            SetColours(primary, secondary, primary, secondary);
        }

        [[nodiscard]] bool IsBlown() const {
            return blown || health <= kWreckHealth;
        }

        bool SetUpgrade(std::uint16_t upgradeId) {
            const int slot = UpgradeSlot(upgradeId);

            if (slot < 0 || slot >= kUpgradeSlots) {
                return false;
            }

            upgrades[static_cast<std::size_t>(slot)] = upgradeId;
            return true;
        }

        void ClearUpgradeSlot(int slot) {
            if (slot >= 0 && slot < kUpgradeSlots) {
                upgrades[static_cast<std::size_t>(slot)] = 0;
            }
        }

        void LatchBlown() {
            if (!IsBlown()) {
                return;
            }

            blown    = true;
            health   = 0.0f;
            engineOn = false;
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override;
        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override;
        void OnStateForced() override;
    };
} // namespace VantaMP::Shared
