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
} // namespace VantaMP::SDK
