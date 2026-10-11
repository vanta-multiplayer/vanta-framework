#pragma once

// Shared state for the vehicle sync subsystem.
//
// INTERNAL TO game/sync/vehicle*. This is not a public interface - it exists
// because the subsystem is one registry with several files acting on it, and
// C++ has no way to share a translation unit. Nothing outside those files may
// include it; everyone else goes through vehicle_sync.h.
//
// The alternative was one 2200-line file, which is what this replaced.

#include "game/sync/vehicle/handler.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/rpc/common/sequence.h"
#include "shared/rpc/common/wire_registry.h"
#include "shared/vehicle/upgrades.h"
#include "shared/vehicle/colors.h"

#include <utils/interpolator.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace VantaMP::Game::VehicleSyncState {
    struct RemoteVehicle {
        void                         *handle = nullptr;
        Framework::Utils::Interpolator interpolator;
        glm::vec3                      aimedPosition {};
        glm::quat                      aimedRotation = glm::identity<glm::quat>();
        float                          speed        = 0.0f;
        std::uint32_t                  whenMs       = 0;
        bool                           aimed        = false;

        glm::vec3 velocity {};
        glm::vec3 angular {};
        float     health   = 1000.0f;
        bool      engineOn = true;
        bool      sirenOn  = false;

        std::int8_t  steer       = 0;
        std::uint8_t accelerator = 0;
        std::uint8_t brake       = 0;
        bool         handbrake   = false;
        std::int8_t  gasPedal    = 0;
        std::int8_t  brakePedal  = 0;

        std::uint8_t  lastEngine  = 0;
        std::uint64_t lastDoors   = 0;
        std::uint32_t lastWheels  = 0;
        std::uint32_t lastPanels  = 0;
        std::uint32_t lastLights  = 0;
        std::uint64_t lastRatios       = 0;
        std::uint64_t lastEntityRatios = 0;
        std::uint64_t heldRatios       = 0;
        float         doorStart[6]     {};
        float         doorTarget[6]    {};
        std::uint32_t doorInterpMs     = 0;
        bool          doorInterp       = false;
        bool          haveDamage  = false;
        bool          wrecked     = false;
        std::uint32_t lastDamageReportMs = 0;
        std::uint32_t lastRatioReportMs  = 0;
        std::uint64_t lastSentRatios     = 0;

        // GetTickCount() when the local ped stopped using a door on this
        // vehicle. Reporting continues for a short while afterwards - see
        // ReportOccupantDoorRatios.
        std::uint32_t doorReleasedMs = 0;

        std::uint8_t  lastPaintjob  = 255;
        std::array<std::uint32_t, Shared::kVehicleColorCount> lastColours {};
        bool haveColours = false;
        std::array<std::uint16_t, Shared::kUpgradeSlots> lastUpgrades {};

        bool         haveScriptState     = false;
        bool         lastEngineOn        = true;
        bool         lastSirenOn         = false;
        std::uint8_t lastOverrideLights  = 0;
        bool         lastLocked          = false;
        std::uint8_t lastDirt            = 255;
        bool         lastTaxiLight       = false;
        std::uint32_t lastHeadlight      = 0;
        bool         lastFrozen          = false;
        bool         lastDamageProof     = false;
        bool         lastCollisions      = true;
        bool         lastLandingGear     = true;
        bool         lastLightsOn        = false;
        bool         lastEngineBroken    = false;
        bool         lastWheelsCanBurst  = true;
        bool         lastCanBeDamaged    = true;
        std::uint8_t lastNitroCount      = 0;
        std::string  lastPlateText;
        std::uint8_t lastVariant         = 255;
        std::uint8_t lastVariant2        = 255;
        bool         haveExtras          = false;
        bool         haveVariants        = false;

        // --- Outbound driver sync (owner side only) ---
        //
        // The last state actually PUT ON THE WIRE, not the last state
        // read from GTA. A delta has to compare against what the
        // receiver believes, or a field that drifts by less than its
        // threshold every tick is never sent at all and the two sides
        // walk apart for ever.
        Shared::RPC::Sequence sentSequence = 0;
        glm::vec3     sentPosition {};
        glm::quat     sentRotation = glm::identity<glm::quat>();
        glm::vec3     sentVelocity {};
        glm::vec3     sentAngular {};
        float         sentHealth   = -1.0f;
        std::int8_t   sentSteer    = 0;
        std::uint8_t  sentAccel    = 0;
        std::uint8_t  sentBrake    = 0;
        std::int8_t   sentGas      = 0;
        std::int8_t   sentBrakePedal = 0;
        std::uint8_t  sentFlags    = 0xFF;
        std::uint32_t lastDriverSyncMs  = 0;
        std::uint32_t lastKeyframeMs    = 0;
        bool          haveSentDriver    = false;

        // --- Unoccupied syncer role ---
        //
        // True only while the server has named THIS client the syncer for
        // this vehicle. Never inferred locally: two clients each deciding
        // they are nearest is exactly the double-owner bug the handoff
        // exists to prevent.
        bool isSyncer = false;

        // Last state this client put on the wire AS SYNCER. Separate from
        // the driver-sync baseline above: the two roles are mutually
        // exclusive for a given vehicle, but a car can pass between them
        // and each must start from its own clean comparison.
        glm::vec3 syncedPosition {};
        glm::quat syncedRotation = glm::identity<glm::quat>();
        glm::vec3 syncedVelocity {};
        glm::vec3 syncedAngular {};
        float     syncedHealth    = -1.0f;
        bool      haveSynced      = false;
        std::uint32_t lastSyncerKeyframeMs = 0;

        // --- Inbound driver sync (receiver side) ---
        //
        // The newest sequence actually APPLIED, so a reordered datagram
        // can be dropped. Per vehicle, not per channel: a sequenced RakNet
        // channel would let one car's packets discard another's.
        Shared::RPC::Sequence appliedSequence = 0;
        bool          haveAppliedSequence = false;
    };


    inline std::unordered_map<std::uint64_t, RemoteVehicle> gVehicles;

    // REVERSE INDEX: GTA handle -> network id.
    //
    // VehicleNetworkIdOf used to walk every entry in gVehicles. It is
    // called every frame from the player sync push and from
    // WatchLocalOccupancy, so on a busy server that was a linear scan of
    // every streamed vehicle, twice a frame, to answer a question the map
    // already knew. Maintained at the only two points where a handle is
    // created or destroyed.
    inline std::unordered_map<const void *, std::uint64_t> gVehicleIdByHandle;

    // Canonical NetworkID <-> compact wire id. The client only ever
    // learns mappings; it never mints one (see wire_registry.h).
    inline Shared::RPC::Common::WireRegistry gWireIds;

    // MTA's stream-in warp: a ped that appeared already in a seat,
    // remembered until the car exists and they are put in it.
    inline std::unordered_map<int, bool> gLateJoinWarp;

    // What the LOCAL player is doing with a vehicle. Drives WatchLocalOccupancy
    // and is read by the occupancy RPC handlers.
    enum class LocalRide { OnFoot, Entering, Seated, Leaving };

    inline LocalRide gLocalRide = LocalRide::OnFoot;

    // Stick held when the climb started must not cancel it - that is just
    // walking up to the door. A new press, a jump, or a release then press is
    // the player changing their mind. Set by the occupancy RPCs, read by the
    // local state machine.
    inline bool gBlockEnterUntilRelease = false;
    inline bool gBlockExitUntilRelease  = false;

    // The GTA handle for a network id, or nullptr if it is not spawned here.
    [[nodiscard]] inline void *HandleOf(std::uint64_t vehicleId) {
        const auto found = gVehicles.find(vehicleId);

        return found != gVehicles.end() ? found->second.handle : nullptr;
    }

    // Rate limiter for the unoccupied batch. Owned by outbound.cpp; lives here
    // because teardown resets it.
    inline std::uint32_t gLastUnoccupiedSendMs = 0;

    // --- lifecycle.cpp ---
    //
    // The GTA handle behind a replica: create, recreate, destroy, cosmetics.
    void EnsureSpawned(Shared::VehicleEntity *entity, RemoteVehicle &remote);
    void DestroyHandle(RemoteVehicle &remote);
    void RecreateIfVariantChanged(Shared::VehicleEntity *entity, RemoteVehicle &remote);
    void ApplyCosmetics(Shared::VehicleEntity *entity, RemoteVehicle &remote);
    void ApplyScriptState(Shared::VehicleEntity *entity, RemoteVehicle &remote);
    void ApplyExtras(Shared::VehicleEntity *entity, RemoteVehicle &remote);

    // --- outbound.cpp ---
    //
    // What this client sends. Driven by the per-frame pass in vehicle_sync.cpp.
    void PushOwnedVehicle(Shared::VehicleEntity *entity, RemoteVehicle &remote);
    void SendUnoccupiedBatches();

    // --- damage_sync.cpp ---
    //
    // Damage and door swing. Declared here because the per-frame pass in
    // vehicle_sync.cpp drives them; everything they touch is in this header.
    void MergeOwnerDamage(Shared::VehicleEntity *entity, RemoteVehicle &remote);
    void ReportBystanderDamage(std::uint64_t vehicleId, Shared::VehicleEntity *entity, RemoteVehicle &remote);
    void ReportOccupantDoorRatios(std::uint64_t vehicleId, Shared::VehicleEntity *entity, RemoteVehicle &remote);
    void PulseRemoteDoorRatios(void *handle, RemoteVehicle &remote);
    void BeginDoorInterp(RemoteVehicle &remote, void *handle, std::uint64_t targetPacked);
    void ApplyDamageToHandle(void *handle, const Shared::VehicleEntity &entity, RemoteVehicle &remote, bool spawnFlying);

    // Late-join bookkeeping, used by the occupancy and lifecycle files.
    void RememberLateJoin(int slot);
    void ForgetLateJoin(int slot);
    [[nodiscard]] bool ConsumeLateJoin(int slot);
} // namespace VantaMP::Game::VehicleSyncState
