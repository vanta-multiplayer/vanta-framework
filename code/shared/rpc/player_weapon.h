#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // Script give / take on the OWNER. The snapshot already carries what
    // they hold; this is what puts a weapon in GTA's hands so the next
    // puresync reports it.
    struct PlayerWeapon {
        static constexpr const char *kIdentifier = "VantaMP::PlayerWeapon";

        static constexpr std::uint8_t kGive    = 0;
        static constexpr std::uint8_t kTake    = 1;
        static constexpr std::uint8_t kTakeAll = 2;

        std::uint8_t  action     = kGive;
        std::uint32_t weaponType = 0;
        std::uint32_t ammo       = 0;

        void FromParameters(std::uint8_t kind, std::uint32_t weapon, std::uint32_t count) {
            action     = kind;
            weaponType = weapon;
            ammo       = count;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, action);
            bs->Serialize(write, weaponType);
            bs->Serialize(write, ammo);
        }
    };
} // namespace VantaMP::Shared::RPC
