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
        bool         reloading = false;
    };

    // What the local player is holding right now.
    [[nodiscard]] PedWeapon QueryLocalPedWeapon();

    // Puts a weapon in the local player's hands and draws it.
    void GiveLocalPedWeapon(unsigned int weaponType, unsigned int ammo);
    void TakeLocalPedWeapon(unsigned int weaponType);
    void TakeAllLocalPedWeapons();
    void SetLocalPedWeaponAmmo(unsigned int weaponType, unsigned int ammo);
    void SetLocalPedWeaponSlot(unsigned int slot);

    // Starts a reload: CTaskSimpleUseGun command 1 (plugin-sdk: 0x1
    // reloading) plus WEAPONSTATE_RELOADING. Writing the state alone is
    // not enough - the anim is an OUTPUT of that command.
    //
    // False when there is nothing to reload (melee, empty reserve, already
    // reloading). Used for the R bind and for an empty clip, which
    // FireInstantHit often never marks as reloading on its own.
    bool ReloadLocalPedWeapon();

    // Puts a weapon in a remote player's hands, from what their owner reports.
    //
    // Safe to call every update: the expensive part only runs when the type
    // actually changes, and ammo alone is two field writes.
    void SetRemotePedWeapon(int slot, unsigned int weaponType, unsigned int ammoClip, unsigned int ammoTotal);

    // Start the reload task on a remote ped. Safe to call every update: a
    // ped already on command 1 is left alone.
    void ReloadRemotePedWeapon(int slot);
} // namespace VantaMP::SDK
