#include "local_ped.h"

#include "access.h"
#include "sdk/world/streaming.h"

namespace VantaMP::SDK {
    LocalPedState QueryLocalPed() {
        LocalPedState state;

        // Null for as long as the game sits in the menus
        const auto ped = Internal::LocalPed();

        if (!ped) {
            return state;
        }

        const auto &position = ped->GetPosition();

        state.valid      = true;
        state.x          = position.x;
        state.y          = position.y;
        state.z          = position.z;
        state.heading    = Internal::HeadingOf(ped);
        state.modelIndex = ped->m_nModelIndex;
        state.pedType    = ped->m_nPedType;
        state.health     = ped->m_fHealth;
        state.maxHealth  = ped->m_fMaxHealth;
        state.armour     = ped->m_fArmour;
        state.dead = Internal::PedLooksDead(ped);

        state.ducked = ped->bIsDucking;

        state.velocityX = ped->m_vecMoveSpeed.x;
        state.velocityY = ped->m_vecMoveSpeed.y;
        state.velocityZ = ped->m_vecMoveSpeed.z;

        state.inWater = Internal::IsPedInWater(ped);

        // SWIMMING IS NOT FALLING, AND THE DIFFERENCE COSTS REAL MONEY.
        //
        // bIsStanding is the game's own "on something solid" flag, and in the
        // air is its absence - but a swimmer is not standing on anything
        // either, so this read true for the entire time a player was in the
        // water. That matters because isInAir is what decides how often the
        // receiver applies velocity: airborne means every single update, since
        // a jump arc has no animation driving it and needs the owner's numbers.
        //
        // A swimmer has the opposite problem. GTA's swim task is computing its
        // move speed from the replayed input already, so writing the owner's on
        // top of it ten times a second is the same movement counted twice, and
        // the ped surges and stalls its way across the water.
        state.inAir = ped->bIsInTheAir || (!ped->bIsStanding && !state.inWater);

        if (ped->bInVehicle && ped->m_pVehicle) {
            state.inVehicle         = true;
            state.vehicleModelIndex = ped->m_pVehicle->m_nModelIndex;
        }

        state.interior = ped->m_nAreaCode;

        return state;
    }

    void ApplyLocalPedDamage(float damage) {
        auto *ped = Internal::LocalPed();

        if (!ped || damage <= 0.0f) {
            return;
        }

        float remaining = damage;

        if (ped->m_fArmour > 0.0f) {
            const float fromArmour = ped->m_fArmour < remaining ? ped->m_fArmour : remaining;

            ped->m_fArmour -= fromArmour;
            remaining -= fromArmour;
        }

        if (remaining > 0.0f) {
            ped->m_fHealth = ped->m_fHealth > remaining ? ped->m_fHealth - remaining : 0.0f;
        }
    }

    void ReviveLocalPed(float x, float y, float z, float heading, float health, float armour) {
        auto *ped = Internal::LocalPed();

        if (!ped) {
            return;
        }

        Internal::StandPedUp(ped);

        ped->m_fHealth    = health;
        ped->m_fMaxHealth = health > ped->m_fMaxHealth ? health : ped->m_fMaxHealth;
        ped->m_fArmour    = armour;

        Internal::SetPedPosition(ped, x, y, z);
        Internal::SetPedRotation(ped, heading);
    }

    void SetLocalPedInterior(std::uint8_t interior) {
        if (auto *ped = Internal::LocalPed()) {
            ped->m_nAreaCode = interior;
        }
    }

    void SetLocalPedTransform(float x, float y, float z, float heading) {
        auto *ped = Internal::LocalPed();

        if (!ped) {
            return;
        }

        Internal::SetPedPosition(ped, x, y, z);
        Internal::SetPedRotation(ped, heading);
    }

    void SetLocalPedVitals(float health, float armour) {
        auto *ped = Internal::LocalPed();

        if (!ped) {
            return;
        }

        ped->m_fHealth    = health;
        ped->m_fMaxHealth = health > ped->m_fMaxHealth ? health : ped->m_fMaxHealth;
        ped->m_fArmour    = armour;
    }

    void SetLocalPedModel(int modelIndex) {
        auto *ped = Internal::LocalPed();

        if (!ped || modelIndex <= 0 || ped->m_nModelIndex == modelIndex) {
            return;
        }

        LoadModelNow(modelIndex);
        reinterpret_cast<void(__thiscall *)(void *, unsigned int)>(gPatterns.CPed__SetModelIndex)(ped, static_cast<unsigned int>(modelIndex));
    }

    void SetLocalPedFrozen(bool frozen) {
        auto *ped = Internal::LocalPed();

        if (!ped) {
            return;
        }

        ped->bDontApplySpeed    = frozen;
        ped->bDisableMoveForce  = frozen;
        ped->bDisableTurnForce  = frozen;

        if (frozen) {
            ped->m_vecMoveSpeed = CVector();
            ped->m_vecTurnSpeed = CVector();
        }
    }

    void SetLocalPlayerMoney(std::int32_t money) {
        if (auto *info = reinterpret_cast<CPlayerInfo *>(Game::PlayerInfoAt(0))) {
            info->m_nMoney        = money;
            info->m_nDisplayMoney = money;
        }
    }

    std::int32_t LocalPlayerMoney() {
        if (const auto *info = reinterpret_cast<const CPlayerInfo *>(Game::PlayerInfoAt(0))) {
            return info->m_nMoney;
        }

        return 0;
    }

    void SetLocalPlayerWanted(std::uint8_t stars) {
        const auto *info = reinterpret_cast<CPlayerInfo *>(Game::PlayerInfoAt(0));

        if (!info || !info->m_PlayerData.m_pWanted) {
            return;
        }

        info->m_PlayerData.m_pWanted->m_nWantedLevel = stars > 6 ? 6 : stars;
    }

    std::uint8_t LocalPlayerWanted() {
        const auto *info = reinterpret_cast<const CPlayerInfo *>(Game::PlayerInfoAt(0));

        if (!info || !info->m_PlayerData.m_pWanted) {
            return 0;
        }

        const auto level = info->m_PlayerData.m_pWanted->m_nWantedLevel;

        return level > 6 ? 6 : static_cast<std::uint8_t>(level);
    }
} // namespace VantaMP::SDK
