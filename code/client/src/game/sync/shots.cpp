#include "shots.h"

#include "game/sync/death.h"
#include "game/sync/player_sync.h"
#include "sdk/ped/local_ped.h"
#include "sdk/ped/shot.h"
#include "sdk/ped/weapon.h"
#include "shared/rpc/player_reload.h"
#include "shared/rpc/player_shot.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <cstdint>
#include <unordered_map>

namespace VantaMP::Game {
    namespace {
        std::uint8_t gShotOrder = 0;

        std::unordered_map<std::uint64_t, std::uint8_t> gLastShotOrder;

        void SendLocalShot(unsigned int weaponType, float ox, float oy, float oz, float tx, float ty, float tz, int hitSlot) {
            auto *net = Framework::CoreModules::GetNetworkPeer();

            if (!net) {
                return;
            }

            const std::uint64_t hitId = hitSlot > 0 ? NetworkIdOfSlot(hitSlot) : 0;
            const float hitDamage     = hitId != 0 ? static_cast<float>(SDK::WeaponDamageOf(weaponType)) : 0.0f;

            Shared::RPC::PlayerShot shot;
            shot.FromParameters(0, weaponType, ox, oy, oz, tx, ty, tz, ++gShotOrder, hitId, hitDamage);
            net->BroadcastRPC(shot);

            // debug
            // Framework::Logging::GetLogger("Shots")->debug("sent shot #{} weapon {} hit {} dmg {:.0f} ({:.1f},{:.1f},{:.1f})->({:.1f},{:.1f},{:.1f})", gShotOrder, weaponType, hitId, hitDamage, ox, oy, oz, tx, ty, tz);
        }
    } // namespace

    void InstallShotPackets() {
        SDK::SetLocalShotReporter(SendLocalShot);
        SDK::InstallShotSync();
    }

    void ReportLocalReload() {
        auto *net = Framework::CoreModules::GetNetworkPeer();

        if (!net) {
            return;
        }

        Shared::RPC::PlayerReload reload;
        reload.FromParameters(0);
        net->BroadcastRPC(reload);
    }

    void CheckLocalReload() {
        const auto weapon = SDK::QueryLocalPedWeapon();

        static bool wasReloading = false;

        // Aimed bursts go through FireInstantHit and often never mark the
        // weapon RELOADING, so a dumped AK clip sits at 0 forever. Start
        // the UseGun reload ourselves, then tell everyone else.
        if (!weapon.reloading && weapon.ammoClip == 0 && weapon.ammoTotal > 0) {
            if (SDK::ReloadLocalPedWeapon()) {
                ReportLocalReload();
            }

            wasReloading = true;
            return;
        }

        if (weapon.reloading && !wasReloading) {
            ReportLocalReload();
        }

        wasReloading = weapon.reloading;
    }

    void ApplyRemoteShot(std::uint64_t networkId, unsigned int weaponType, float ox, float oy, float oz, float tx, float ty, float tz, std::uint8_t order, std::uint64_t damagedNetworkId, float damage) {
        const int slot = SlotOfNetworkId(networkId);

        if (slot < 0) {
            // debug
            // Framework::Logging::GetLogger("Shots")->debug("shot from {} dropped; no ped yet", networkId);
            return;
        }

        auto &last = gLastShotOrder[networkId];

        // Signed wrap, the way MTA compares its order byte: a late duplicate
        // of the last shot is ignored; a wrap past 255 is still "newer".
        if (static_cast<std::int8_t>(order - last) <= 0 && last != 0) {
            return;
        }

        last = order;

        // debug
        // Framework::Logging::GetLogger("Shots")->debug("apply shot #{} from {} slot {} weapon {} hit {}", order, networkId, slot, weaponType, damagedNetworkId);

        // Health is ours to lose. The ray is the owner's, but interpolation
        // can put this ped off it; MTA then applies the damage the shooter
        // already measured. Same here, and only if FireInstantHit did not.
        const bool namedUs = damagedNetworkId != 0 && damagedNetworkId == LocalNetworkId() && damage > 0.0f;
        const auto before  = namedUs ? SDK::QueryLocalPed() : SDK::LocalPedState {};

        SDK::FireRemotePedShot(slot, weaponType, ox, oy, oz, tx, ty, tz);

        if (namedUs) {
            const auto after = SDK::QueryLocalPed();

            if (after.health >= before.health && after.armour >= before.armour) {
                SDK::ApplyLocalPedDamage(damage);
            }

            RememberLocalDamager(networkId, weaponType);
        }
    }

    void ApplyRemoteReload(std::uint64_t networkId) {
        const int slot = SlotOfNetworkId(networkId);

        if (slot < 0) {
            return;
        }

        SDK::ReloadRemotePedWeapon(slot);
    }
} // namespace VantaMP::Game
