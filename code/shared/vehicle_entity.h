#pragma once

#include <networking/replication/network_entity.h>

#include <cstdint>

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

        // --- Construction ------------------------------------------------------

        std::uint32_t modelId         = 0;
        std::uint8_t  primaryColour   = 0;
        std::uint8_t  secondaryColour = 0;

        // --- Per-tick ----------------------------------------------------------

        glm::vec3 angularVelocity {};

        float health = 1000.0f;

        bool engineOn = true;
        bool sirenOn  = false;

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

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override;
        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override;
    };
} // namespace VantaMP::Shared
