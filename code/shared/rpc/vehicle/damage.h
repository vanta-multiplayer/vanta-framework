#pragma once

#include "common.h"

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // Damage a client can see on a vehicle, reported as an event.
    //
    // DAMAGE IS NOT MOTION STATE. It changes rarely and must not be lost, so
    // it rides its own reliable packet instead of being re-sent inside every
    // 100 ms motion snapshot. That separation is the single biggest reason
    // the motion packet is small.
    //
    // OMITTED MEANS DO NOT MODIFY. This is not a style preference here: the
    // server merges worse-wins, and merging against an absent group read as
    // zero would silently REPAIR damage that the reporter simply had nothing
    // to say about. Door ratios are worse: they are REPLACED rather than
    // merged, so an absent group read as zero slams every door shut.
    struct VehicleDamageReport {
        static constexpr const char *kIdentifier = "VantaMP::VehicleDamageReport";

        std::uint64_t vehicleId = 0;

        std::uint8_t fields = 0;

        std::uint8_t  damageEngine     = 0;
        std::uint64_t doorsPacked      = 0;
        std::uint32_t wheelsPacked     = 0;
        std::uint32_t panels           = 0;
        std::uint32_t lights           = 0;
        std::uint64_t doorRatiosPacked = 0;
        float         health           = 1000.0f;

        [[nodiscard]] bool Has(std::uint8_t field) const {
            return (fields & field) != 0;
        }

        void Mark(std::uint8_t field) {
            fields |= field;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, vehicleId);

            if (write) {
                bs->WriteBits(reinterpret_cast<const unsigned char *>(&fields), Vehicle::DamageField::kCount);
            }
            else {
                fields = 0;
                bs->ReadBits(reinterpret_cast<unsigned char *>(&fields), Vehicle::DamageField::kCount);
            }

            if (Has(Vehicle::DamageField::kEngine)) {
                bs->Serialize(write, damageEngine);
            }

            if (Has(Vehicle::DamageField::kDoors)) {
                bs->Serialize(write, doorsPacked);
            }

            if (Has(Vehicle::DamageField::kWheels)) {
                bs->Serialize(write, wheelsPacked);
            }

            if (Has(Vehicle::DamageField::kPanels)) {
                bs->Serialize(write, panels);
            }

            if (Has(Vehicle::DamageField::kLights)) {
                bs->Serialize(write, lights);
            }

            if (Has(Vehicle::DamageField::kDoorRatios)) {
                bs->Serialize(write, doorRatiosPacked);
            }

            if (Has(Vehicle::DamageField::kHealth)) {
                Vehicle::SerializeHealth(bs, write, health);
            }
        }
    };
} // namespace VantaMP::Shared::RPC
