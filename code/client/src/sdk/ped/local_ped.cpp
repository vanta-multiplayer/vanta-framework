#include "local_ped.h"

#include "access.h"

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

        state.ducked = ped->bIsDucking;

        state.velocityX = ped->m_vecMoveSpeed.x;
        state.velocityY = ped->m_vecMoveSpeed.y;
        state.velocityZ = ped->m_vecMoveSpeed.z;

        // bIsStanding is the game's own "on something solid" flag; in the air
        // is its absence, which is what decides a falling animation.
        state.inAir   = ped->bIsInTheAir || !ped->bIsStanding;
        state.inWater = ped->bSubmergedInWater;

        if (ped->bInVehicle && ped->m_pVehicle) {
            state.inVehicle         = true;
            state.vehicleModelIndex = ped->m_pVehicle->m_nModelIndex;
        }

        return state;
    }
} // namespace VantaMP::SDK
