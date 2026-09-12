#include "death.h"

#include "game/entities/local_player.h"
#include "sdk/ped/local_ped.h"
#include "shared/rpc/player/death.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

namespace VantaMP::Game {
    namespace {
        bool          gDeathReported = false;
        std::uint64_t gLastKillerId  = 0;
        unsigned int  gLastWeapon    = 0;
    } // namespace

    void RememberLocalDamager(std::uint64_t killerNetworkId, unsigned int weaponType) {
        gLastKillerId = killerNetworkId;
        gLastWeapon   = weaponType;
    }

    void ClearLocalDeathReport() {
        gDeathReported = false;
        gLastKillerId  = 0;
        gLastWeapon    = 0;
    }

    void CheckLocalDeath() {
        if (gDeathReported || !IsPlayerSpawned()) {
            return;
        }

        const auto local = SDK::QueryLocalPed();

        if (!local.valid || !local.dead) {
            return;
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        gDeathReported = true;

        Shared::RPC::PlayerDeath death;
        death.FromParameters(0, gLastKillerId, gLastWeapon);
        net->BroadcastRPC(death);
    }
} // namespace VantaMP::Game
