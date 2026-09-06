#pragma once

#include <cstdint>

namespace VantaMP::Game {
    // Wires CWeapon::Fire to PlayerShot / PlayerReload packets.
    void InstallShotPackets();

    // The local player started a reload. Sent as its own packet so remotes
    // play the animation once, rather than inferring it from ammo on the tick.
    void ReportLocalReload();

    // Empty-clip and the game's own RELOADING edge. Aimed fire often never
    // sets the state, so without this a dumped magazine stays empty.
    void CheckLocalReload();

    void ApplyRemoteShot(std::uint64_t networkId, unsigned int weaponType, float ox, float oy, float oz, float tx, float ty, float tz, std::uint8_t order, std::uint64_t damagedNetworkId, float damage);
    void ApplyRemoteReload(std::uint64_t networkId);
} // namespace VantaMP::Game
