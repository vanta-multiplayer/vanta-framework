#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // Damage the sender can see on a car they do not own.
    //
    // Occupied motion is the driver's. A bystander who shoots the hood
    // still opens it on their own client; without this the owner never
    // hears about it and their next pose push slams it shut for everyone.
    // The server keeps the worse of what it has and what arrives.
    struct VehicleDamageReport {
        static constexpr const char *kIdentifier = "VantaMP::VehicleDamageReport";

        std::uint64_t vehicleId        = 0;
        std::uint8_t  damageEngine     = 0;
        std::uint64_t doorsPacked      = 0;
        std::uint32_t wheelsPacked     = 0;
        std::uint32_t panels           = 0;
        std::uint32_t lights           = 0;
        std::uint64_t doorRatiosPacked = 0;
        float         health           = 1000.0f;
        // Status-only reports must not zero the swing. A hood punch used
        // to send ratios as 0, and replacing with that slammed every door.
        bool applyRatios = false;

        void FromParameters(std::uint64_t id, std::uint8_t engine, std::uint64_t doors, std::uint32_t wheels, std::uint32_t panelBits, std::uint32_t lightBits,
                            std::uint64_t ratios, bool ratiosLive = false, float hp = 1000.0f) {
            vehicleId        = id;
            damageEngine     = engine;
            doorsPacked      = doors;
            wheelsPacked     = wheels;
            panels           = panelBits;
            lights           = lightBits;
            doorRatiosPacked = ratios;
            applyRatios      = ratiosLive;
            health           = hp;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, vehicleId);
            bs->Serialize(write, damageEngine);
            bs->Serialize(write, doorsPacked);
            bs->Serialize(write, wheelsPacked);
            bs->Serialize(write, panels);
            bs->Serialize(write, lights);
            bs->Serialize(write, doorRatiosPacked);
            bs->Serialize(write, applyRatios);
            bs->Serialize(write, health);
        }
    };
} // namespace VantaMP::Shared::RPC
