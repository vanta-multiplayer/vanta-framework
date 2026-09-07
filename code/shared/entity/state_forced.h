#pragma once

namespace VantaMP::Shared {
    class PlayerEntity;
    class VehicleEntity;

    // The shared entity types live on both ends. Applying a ForceState
    // (script wrote position / rotation) has to move the GTA ped or car,
    // which only the client can do. The client registers these at init.
    using PlayerForcedFn  = void (*)(PlayerEntity *);
    using VehicleForcedFn = void (*)(VehicleEntity *);

    inline PlayerForcedFn  gOnPlayerForced  = nullptr;
    inline VehicleForcedFn gOnVehicleForced = nullptr;
} // namespace VantaMP::Shared
