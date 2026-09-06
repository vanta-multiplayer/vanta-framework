#pragma once

namespace VantaMP::SDK {
    // What a ped is holding.
    //
    // A ped carries up to thirteen weapons at once and holds one of them, so
    // this is the SELECTED slot rather than an inventory. Type 0 is unarmed,
    // which is a real answer and not an absent one.
    struct PedWeapon {
        unsigned int type      = 0;
        unsigned int ammoClip  = 0;
        unsigned int ammoTotal = 0;
    };

    // What the local player is holding right now.
    [[nodiscard]] PedWeapon QueryLocalPedWeapon();

    // Puts a weapon in the local player's hands and draws it.
    void GiveLocalPedWeapon(unsigned int weaponType, unsigned int ammo);

    // Puts a weapon in a remote player's hands, from what their owner reports.
    //
    // Safe to call every update: the expensive part only runs when the type
    // actually changes, and ammo alone is two field writes.
    void SetRemotePedWeapon(int slot, unsigned int weaponType, unsigned int ammoClip, unsigned int ammoTotal);
} // namespace VantaMP::SDK
