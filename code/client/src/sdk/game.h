#pragma once

namespace VantaMP::SDK {
    // Plain on purpose: plugin-sdk defines RW and drags RenderWare in, so it stays behind this.
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
    };

    LocalPedState QueryLocalPed();

    // --- Remote peds ---------------------------------------------------------
    //
    // Creating a ped needs plugin-sdk's CPlayerPed constructor and a dozen of
    // its fields, so it lives behind this boundary with the rest of the SDK.
    // Everything above the boundary works in slots and plain numbers.

    // Builds a ped for `slot` and puts it in the world.
    //
    // The sequence mirrors what the game does for the local player, in the same
    // order. Every step in it exists because leaving it out produced something
    // specific and unpleasant - see the implementation.
    //
    // Returns the ped, or nullptr if it could not be created.
    void *CreateRemotePed(int slot, int modelIndex, float x, float y, float z);

    // Removes a ped created by CreateRemotePed.
    void DestroyRemotePed(int slot);

    // Moves a ped that already exists. Position and heading only - velocity and
    // animation come from the game processing the ped's input.
    void SetRemotePedTransform(int slot, float x, float y, float z, float heading);

    void SetRemotePedHealth(int slot, float health, float armour);
} // namespace VantaMP::SDK
