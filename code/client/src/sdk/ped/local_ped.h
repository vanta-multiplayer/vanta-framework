#pragma once

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
    };

    // What the local player is HOLDING is in sdk/ped/weapon.h, not here.
    // Weapons are their own subsystem - a model to stream, a slot to select, a
    // clip to track - rather than another few fields of ped state.
    LocalPedState QueryLocalPed();
} // namespace VantaMP::SDK
