#pragma once

#include <cstdint>
#include <string>

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
    [[nodiscard]] void *CreateScriptVehicle(int modelId, float x, float y, float z, float heading, std::uint8_t primaryColour, std::uint8_t secondaryColour,
                                            std::uint8_t variant = 255, std::uint8_t variant2 = 255);

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
        bool         handbrake   = false;

        // Signed copies of the two pedals. m_fGasPedal is -1..1 with negative
        // meaning reverse, which the unsigned pair above cannot represent.
        std::int8_t  gasPedal    = 0;
        std::int8_t  brakePedal  = 0;

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

    // Health / engine / siren without touching move or turn speed. Use this
    // rather than a read-modify-write of ApplyVehicleMotion, which would
    // re-impose a one-frame-old velocity - see the comment on the definition.
    void ApplyVehicleStates(void *vehicle, float health, bool engineOn, bool sirenOn);

    // Per door/panel, the way MTA's damage sync does it. A full
    // SetupDamageAfterLoad on every packet respawns flying parts and
    // slams the ratios. spawnFlying is SetDoorDamage's visual; leave
    // it off for stream-in and while someone is in contact.
    void ApplyVehicleDamage(void *vehicle, std::uint8_t engine, std::uint64_t doorsPacked, std::uint32_t wheelsPacked, std::uint32_t panels, std::uint32_t lights,
                            bool spawnFlying = false);

    // skipMask bit i leaves CDoor i to the enter/leave task. Writing
    // OpenDoor over a climb is what slammed the passenger door shut.
    void ApplyVehicleDoorRatios(void *vehicle, std::uint64_t doorRatiosPacked, std::uint8_t skipMask = 0);

    // One door, forced past the occupancy mask. For the authoritative angle
    // that arrives with an aborted entry.
    void ForceVehicleDoorRatio(void *vehicle, std::uint8_t doorIndex, std::uint8_t ratio);

    void ApplyVehicleControls(void *vehicle, std::int8_t steer, std::uint8_t accelerator, std::uint8_t brake);

    // Steer angle and handbrake for a remote BIKE, straight from the owner.
    // CBike::ProcessControlInputs (0x6BE310) - the only writer of
    // m_fSteerAngle and the only thing that clears the constructor's default
    // handbrake - does not run for a remote bike, so both are copied instead.
    void ApplyBikeDriverOutputs(void *vehicle, std::int8_t steer, bool handbrake, std::int8_t gasPedal, std::int8_t brakePedal);

    void SetVehiclePose(void *vehicle, float x, float y, float z, const glm::quat &rotation);

    // Moves a vehicle without touching its orientation. For a ridden remote
    // bike, whose matrix basis belongs to GTA between packets.
    void SetVehiclePosition(void *vehicle, float x, float y, float z);

    // Corrects where a bike is on the map without touching Z or the basis -
    // both of which are suspension output on a grounded bike.
    void SetBikeHorizontalPosition(void *vehicle, float x, float y);

    // Turns a bike to a network heading while preserving GTA's own pitch and
    // roll. Yaw is navigation; pitch/roll are suspension and lean.
    void SetBikeHeading(void *vehicle, const glm::quat &rotation);

    // Eases a bike's ROLL toward the owner's, keeping GTA's pitch and
    // heading. For the wheelie/airborne case, where GTA's own roll
    // stabilisation does not run on a remote bike.
    void BlendBikeRoll(void *vehicle, const glm::quat &rotation, float alpha);

    // Eases the whole basis toward the owner's, keeping position. For the
    // wheelie case, where the remote bike is not performing the manoeuvre at
    // all and pitch must be taken from the owner too.
    void BlendBikeOrientation(void *vehicle, const glm::quat &rotation, float alpha);

    // The bike's live orientation. The "current" an interpolator aims from.
    [[nodiscard]] glm::quat BikeOrientation(const void *vehicle);

    // Writes an orientation the caller has already decided on, with no
    // blending of its own - the caller's interpolator owns the easing.
    void SetBikeOrientation(void *vehicle, const glm::quat &rotation);

    [[nodiscard]] int   VehicleContactWheels(const void *vehicle);

    // How many of a bike's four suspension lines found no ground: 0 grounded,
    // 4 airborne, in between a wheelie or stoppie. Use this, not the contact
    // count - a grounded bike reports 4 contacts, not 2.
    [[nodiscard]] int   BikeWheelsOffGround(const void *vehicle);

    // GTA's own wheelie measure: the forward vector's Z. 0 level, positive
    // nose-up (wheelie), negative nose-down (stoppie). Compare against
    // BikeWheelieAngle, which is m_fWheelieAng in the same sin() space.
    [[nodiscard]] float BikeForwardZ(const void *vehicle);
    [[nodiscard]] float BikeWheelieAngle(const void *vehicle);
    [[nodiscard]] float VehicleSteerAngle(const void *vehicle);
    [[nodiscard]] bool  VehicleHandbrakeOn(const void *vehicle);

    // TEMPORARY BIKE DIAGNOSTIC. Remove with LogBikeBasis.
    [[nodiscard]] bool IsBikeHandle(const void *vehicle);

    // CEntity::m_nAreaCode on the car. Same visibility rule as peds.
    // Keeps GTA's simulation status in step with who is actually driving.
    // STATUS_PLAYER is what gates ProcessControlInputs (steer, lean, and the
    // clearing of the constructor's default handbrake) and the strong
    // knock-off-bike resistance. Call it every frame for a remote vehicle.
    void SyncVehicleSimulationStatus(void *vehicle);

    void SetVehicleInterior(void *vehicle, std::uint8_t interior);
    [[nodiscard]] std::uint8_t VehicleInterior(const void *vehicle);

    void SetVehicleColors(void *vehicle, std::uint8_t primary, std::uint8_t secondary);

    // Four 0x00RRGGBB colours. GTA only stores palette indexes; these
    // are painted by rewriting ms_vehicleColourTable around SetupRender.
    void SetVehicleRgbColors(void *vehicle, const std::uint32_t *colours, int count);
    void ClearVehicleRgbColors(void *vehicle);

    // Headlight beam/corona tint. 0 = default white. Stored per handle and
    // applied from DoHeadLight* hooks (MTA's approach).
    void SetVehicleHeadlightColor(void *vehicle, std::uint32_t packedRgb);
    void ClearVehicleHeadlightColor(void *vehicle);

    // paintjob 3 is none. upgrades is 17 slots, 0 empty, else 1000–1193.
    void ApplyVehicleCosmetics(void *vehicle, std::uint8_t paintjob, const std::uint16_t *upgrades, int upgradeSlots);

    // Script-owned switches. Owner applies these, then reads GTA back —
    // writing engine here is what makes car.engine = false stick.
    void ApplyVehicleScriptState(void *vehicle, bool engineOn, bool sirenOn, std::uint8_t overrideLights, bool locked);

    // Dirt 0–15, taxi lamp, freeze, proof bits, collision, and plane
    // landing gear.
    void ApplyVehicleExtras(void *vehicle, std::uint8_t dirtLevel, bool taxiLight, std::uint32_t headlightColor, bool frozen, bool damageProof,
                            bool collisions, bool landingGearDown, bool lightsOn, bool engineBroken, bool wheelsCanBurst, bool canBeDamaged,
                            std::uint8_t nitroCount);

    // Rebuilds the per-instance custom plate texture (max 8 chars). Empty
    // clears the custom plate so GTA's stock one shows.
    void SetVehiclePlateText(void *vehicle, const std::string &text);

    // GTA rewrites clump alpha from UpdateClumpAlpha. Call every frame.
    void SetVehicleAlpha(void *vehicle, std::uint8_t alpha);

    // True when the gear is down, or when this is not a plane.
    [[nodiscard]] bool VehicleLandingGearDown(const void *vehicle);
    [[nodiscard]] bool VehicleLightsOn(const void *vehicle);
    [[nodiscard]] bool VehicleOnGround(const void *vehicle);

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
    // Headlight tint — game/patches/vehicle_lights.cpp (framework inject_hook).
    void InstallVehicleHeadlightHooks();
} // namespace VantaMP::SDK
