#pragma once

namespace VantaMP::SDK {
    // Where a ped is pointing its gun.
    //
    // GTA WORKS THIS OUT FROM THE LOCAL CAMERA, FOR EVERY PED, because it was
    // built for one player. The value is computed and passed straight into
    // CPedIK::PointGunInDirection, so that call is intercepted and both angles
    // replaced per ped.
    //
    // BOTH AXES, not only pitch. The horizontal used to be left to the camera
    // rotation we swap per slot, which is right for walking and wrong for
    // aiming: that swap is a 10 Hz snap, and the hands follow it, so a remote
    // player who is aiming stutters at the wrists. MTA substitutes both for
    // the same reason.

    struct PedAim {
        float horizontal = 0.0f;
        float vertical   = 0.0f;
    };

    // What the local player is pointing at, captured the last time the game
    // aimed their gun. Exists only while that call runs, so this is the same
    // number the game just used, one frame old at worst.
    [[nodiscard]] PedAim ReadLocalPedAim();

    void SetRemotePedAim(int slot, float horizontal, float vertical);

    [[nodiscard]] PedAim GetRemotePedAim(int slot);

    // Patches the three call sites. Once, at startup.
    void InstallAimSync();
} // namespace VantaMP::SDK
