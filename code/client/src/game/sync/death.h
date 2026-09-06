#pragma once

#include <cstdint>

namespace VantaMP::Game {
    // Whoever last hurt this client, remembered so a death packet can name
    // them. Cleared on respawn. Zero means the death had no shooter.
    void RememberLocalDamager(std::uint64_t killerNetworkId, unsigned int weaponType);

    // Once per death, from the game tick. The server then fires playerDeath
    // so a resource can spawn them again.
    void CheckLocalDeath();

    // The spawn arrived; the next time health hits zero is a new death.
    void ClearLocalDeathReport();
}
