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

    // Per door/panel, the way MTA's damage sync does it. A full
    // SetupDamageAfterLoad on every packet respawns flying parts and
    // slams the ratios. spawnFlying is SetDoorDamage's visual; leave
    // it off for stream-in and while someone is in contact.
    void ApplyVehicleDamage(void *vehicle, std::uint8_t engine, std::uint64_t doorsPacked, std::uint32_t wheelsPacked, std::uint32_t panels, std::uint32_t lights,
                            bool spawnFlying = false);

    // skipMask bit i leaves CDoor i to the enter/leave task. Writing
    // OpenDoor over a climb is what slammed the passenger door shut.
    void ApplyVehicleDoorRatios(void *vehicle, std::uint64_t doorRatiosPacked, std::uint8_t skipMask = 0);

    void ApplyVehicleControls(void *vehicle, std::int8_t steer, std::uint8_t accelerator, std::uint8_t brake);

    void SetVehiclePose(void *vehicle, float x, float y, float z, const glm::quat &rotation);

    // CEntity::m_nAreaCode on the car. Same visibility rule as peds.
    void SetVehicleInterior(void *vehicle, std::uint8_t interior);
    [[nodiscard]] std::uint8_t VehicleInterior(const void *vehicle);

    void SetVehicleColors(void *vehicle, std::uint8_t primary, std::uint8_t secondary);

    // Four 0x00RRGGBB colours. GTA only stores palette indexes; these
    // are painted by rewriting ms_vehicleColourTable around SetupRender.
    void SetVehicleRgbColors(void *vehicle, const std::uint32_t *colours, int count);
    void ClearVehicleRgbColors(void *vehicle);

    // paintjob 3 is none. upgrades is 17 slots, 0 empty, else 1000–1193.
    void ApplyVehicleCosmetics(void *vehicle, std::uint8_t paintjob, const std::uint16_t *upgrades, int upgradeSlots);

    // Script-owned switches. Owner applies these, then reads GTA back —
    // writing engine here is what makes car.engine = false stick.
    void ApplyVehicleScriptState(void *vehicle, bool engineOn, bool sirenOn, std::uint8_t overrideLights, bool locked);

    [[nodiscard]] bool IsLocalPlayerDriving(const void *vehicle);
    [[nodiscard]] bool VehicleHasDriver(const void *vehicle);

    // GTA starts the blow-up sequence around 250. Writing health alone
    // does not wreck the mesh; a late joiner would see a pristine car.
    inline constexpr float kWreckHealth = 250.0f;

    [[nodiscard]] bool IsVehicleWrecked(const void *vehicle);

    // hideExplosion is MTA's stream-in Blow: the wreck, not another boom.
    void WreckVehicle(void *vehicle, bool hideExplosion);

    // BlowUpCar clears the mission lock and starts GTA's 60-second wreck
    // garbage collection. Call after create and after wreck, and keep
    // calling on a live wreck so streaming cannot delete the hull.
    void KeepScriptVehicle(void *vehicle);

    // MTA ~CClientGame: explosions, projectiles, then every fire in
    // range. Do this before the cars go - CWorld::Remove leaves the
    // slots burning and the next connect menu sits over them.
    void ClearTransientWorldEffects();

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
    void InstallVehicleColorHooks();
} // namespace VantaMP::SDK
