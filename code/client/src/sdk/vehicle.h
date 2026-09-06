#pragma once

#include <cstdint>

#include <glm/gtc/quaternion.hpp>

namespace VantaMP::SDK {
    inline constexpr int kFirstVehicleModel = 400;
    inline constexpr int kLastVehicleModel  = 611;

    [[nodiscard]] bool IsVehicleModel(int modelId);

    // Through CCarCtrl::CreateCarForScript, which is what mission scripts
    // call: pool allocation, world add, physics. Mission cleanup is off so
    // the game never deletes a Vanta car behind us.
    //
    // Returns nullptr when the model is not resident. Request it and try
    // again next tick - a blocking load from inside the streamer hangs.
    [[nodiscard]] void *CreateScriptVehicle(int modelId, float x, float y, float z, float heading, std::uint8_t primaryColour, std::uint8_t secondaryColour);

    void DestroyScriptVehicle(void *vehicle);

    struct VehicleState {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        float vx = 0.0f;
        float vy = 0.0f;
        float vz = 0.0f;

        glm::quat rotation = glm::identity<glm::quat>();

        float avx = 0.0f;
        float avy = 0.0f;
        float avz = 0.0f;

        float health = 1000.0f;

        bool engineOn = true;
        bool sirenOn  = false;

        std::int8_t  steer       = 0;
        std::uint8_t accelerator = 0;
        std::uint8_t brake       = 0;

        std::uint8_t  damageEngine     = 0;
        std::uint64_t doorsPacked      = 0;
        std::uint32_t wheelsPacked     = 0;
        std::uint32_t panels           = 0;
        std::uint32_t lights           = 0;
        std::uint64_t doorRatiosPacked = 0;
        bool          hasDamage        = false;
    };

    [[nodiscard]] VehicleState ReadVehicle(const void *vehicle);

    // Pose, momentum, health, engine, siren. Steer and pedals are
    // outputs of ProcessControl from the swapped pad - writing them
    // here snaps a remote turn every packet.
    void ApplyVehicleMotion(void *vehicle, const VehicleState &state);

    // rebuildModel is SetupDamageAfterLoad: needed to drop a torn-off door,
    // unsafe while a ped is in contact (tank punch) or the car is blowing.
    void ApplyVehicleDamage(void *vehicle, std::uint8_t engine, std::uint64_t doorsPacked, std::uint32_t wheelsPacked, std::uint32_t panels, std::uint32_t lights,
                            bool rebuildModel = true);

    // skipMask bit i leaves CDoor i to the enter/leave task. Writing
    // OpenDoor over a climb is what slammed the passenger door shut.
    void ApplyVehicleDoorRatios(void *vehicle, std::uint64_t doorRatiosPacked, std::uint8_t skipMask = 0);

    void ApplyVehicleControls(void *vehicle, std::int8_t steer, std::uint8_t accelerator, std::uint8_t brake);

    void SetVehiclePose(void *vehicle, float x, float y, float z, const glm::quat &rotation);

    [[nodiscard]] bool IsLocalPlayerDriving(const void *vehicle);
    [[nodiscard]] bool VehicleHasDriver(const void *vehicle);

    // Hooks this vehicle class's ProcessControl so a remote driver can
    // actually steer. m_fSteerAngle is an output: GTA recomputes it from
    // GetPad(0) every frame, which is why writing the angle never turned
    // the wheels. MTA swaps the local pad for the remote driver's; we
    // do the same, once per vehicle subclass, from the live vtable.
    //
    void BindRemoteVehicleProcessControl(void *vehicle);

    // KillPedsInVehicle / KillPedsGettingInVehicle. A tank shot reaches
    // those even when BlowUpCar is not taken through the vtable we patch.
    void InstallVehicleExplosionHooks();
} // namespace VantaMP::SDK
