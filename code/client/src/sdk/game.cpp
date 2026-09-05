#include "game.h"

#include "game_sdk.h"
#include "patterns.h"

#include <cmath>

static_assert(sizeof(CPlaceable) == 0x18, "CPlaceable layout changed");
static_assert(sizeof(CEntity) == 0x38, "CEntity layout changed");

namespace VantaMP::SDK {
    namespace {
        CPed *LocalPed() {
            return *reinterpret_cast<CPed **>(gPatterns.CWorld__Players);
        }

        // plugin-sdk only declares CPlaceable::GetHeading; its definition is at a 1.0 US address.
        float HeadingOf(const CPed *ped) {
            if (!ped->m_matrix) {
                return ped->m_placement.m_fHeading;
            }
            return std::atan2(-ped->m_matrix->right.x, ped->m_matrix->right.y);
        }
    } // namespace

    LocalPedState QueryLocalPed() {
        LocalPedState state;

        // Null for as long as the game sits in the menus
        const auto ped = LocalPed();
        if (!ped) {
            return state;
        }

        const auto &position = ped->GetPosition();

        state.valid      = true;
        state.x          = position.x;
        state.y          = position.y;
        state.z          = position.z;
        state.heading    = HeadingOf(ped);
        state.modelIndex = ped->m_nModelIndex;
        state.pedType    = ped->m_nPedType;
        state.health     = ped->m_fHealth;
        state.maxHealth  = ped->m_fMaxHealth;

        if (ped->bInVehicle && ped->m_pVehicle) {
            state.inVehicle         = true;
            state.vehicleModelIndex = ped->m_pVehicle->m_nModelIndex;
        }

        return state;
    }
} // namespace VantaMP::SDK
