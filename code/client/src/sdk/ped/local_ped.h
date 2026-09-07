#pragma once

#include <cstdint>

namespace VantaMP::SDK {
    // What the local player's ped currently is.
    //
    // Plain on purpose: plugin-sdk defines RW and drags RenderWare in with it,
    // so it stays behind this.
    struct LocalPedState {
        bool valid = false;

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        float heading = 0.0f;

        int modelIndex = 0;
        int pedType    = 0;

        float health    = 0.0f;
        float maxHealth = 0.0f;
        float armour    = 0.0f;

        // Health at or below zero, or the ped already in DIE/DEAD. Either
        // is enough to report a death; waiting only on the state misses a
        // frame where damage wrote 0 and the anim has not started yet.
        bool dead = false;

        bool inVehicle        = false;
        int vehicleModelIndex = 0;

        // Pose the game is actually in, which is not always what the input
        // asked for - see PlayerEntity::isDucked.
        bool ducked  = false;
        bool inAir   = false;
        bool inWater = false;

        // Metres per game unit of time, as the game stores it.
        float velocityX = 0.0f;
        float velocityY = 0.0f;
        float velocityZ = 0.0f;

        // CEntity::m_nAreaCode. 0 is outside. Walking through an enex
        // writes this; the snapshot reports it so remotes can hide.
        std::uint8_t interior = 0;
    };

    // What the local player is HOLDING is in sdk/ped/weapon.h, not here.
    // Weapons are their own subsystem - a model to stream, a slot to select, a
    // clip to track - rather than another few fields of ped state.
    LocalPedState QueryLocalPed();

    // Armour first, then health, the way GTA's damage response does.
    // Used when a PlayerShot names this client as the victim and the
    // replayed ray missed because interpolation put the ped elsewhere.
    void ApplyLocalPedDamage(float damage);

    // Take a dead (or newly spawned) local ped back to standing at a point.
    // MTA spawnPlayer: SetInitialState, then health/armour/heading. Writing
    // health alone leaves the DIE task running and they stay on the ground.
    void ReviveLocalPed(float x, float y, float z, float heading, float health, float armour);

    // Writes the local ped's area code. Pair with SetCurrentInterior when
    // the change did not come from GTA itself.
    void SetLocalPedInterior(std::uint8_t interior);

    // Script wrote player.position / rotation. Move the ped; do not revive.
    void SetLocalPedTransform(float x, float y, float z, float heading);
} // namespace VantaMP::SDK
