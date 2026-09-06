#pragma once

namespace VantaMP::SDK {
    // Intercepts CWeapon::FireInstantHit (the shot itself), swallows remote
    // CWeapon::Fire, and patches the two FireInstantHit call sites MTA hooks:
    // the ProcessLineOfSight that owns the real ray, and the IsPlayer test that
    // would otherwise aim a remote CPlayerPed with this client's camera.
    void InstallShotSync();

    // Called once per local FireInstantHit that actually fired, with the
    // start/end of that bullet and the slot of whoever it hit (-1 if nobody
    // we own). Aimed bursts skip CWeapon::Fire, so this is not a Fire hook.
    void SetLocalShotReporter(void (*report)(unsigned int weaponType, float ox, float oy, float oz, float tx, float ty, float tz, int hitSlot));

    // Replay one received shot on a remote ped, along the owner's line.
    // MTA FireBullet: ignore the shooter, FireInstantHit, then the muzzle
    // flash the IsPlayer patch would otherwise skip (DoGunFlash, particles,
    // shell, point light, fire audio).
    void FireRemotePedShot(int slot, unsigned int weaponType, float ox, float oy, float oz, float tx, float ty, float tz);

    // CWeaponInfo::m_nDamage for this type, or 0.
    [[nodiscard]] unsigned int WeaponDamageOf(unsigned int weaponType);
}
