#include "game/sync/vehicle_sync.h"

#include "game/sync/vehicle/state.h"
#include "sdk/game.h"
#include "sdk/world/streaming.h"
#include "shared/entity/vehicle_entity.h"
#include "shared/vehicle/colors.h"
#include "shared/vehicle/upgrades.h"

#include <logging/logger.h>

#include <glm/gtc/matrix_transform.hpp>

// SPAWNING, DESPAWNING AND COSMETICS.
//
// A remote vehicle is a GTA handle this client created to stand in for a
// replica. This file owns that handle's whole life: streaming the model in,
// creating it, recreating it when a component variant changes (extras are
// baked at construction, so they cannot be set afterwards), and destroying it.
//
// Cosmetics live here for the same reason - colours, paintjob, upgrades and
// plate are construction-shaped state the owner echoes rather than reports.
//
// Split out of vehicle_sync.cpp. Shared registry state is in vehicle/state.h.
namespace VantaMP::Game::VehicleSyncState {
    void ApplyCosmetics(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
        if (!remote.handle) {
            return;
        }

        const bool colours = !remote.haveColours || remote.lastColours != entity->colours;
        const bool paint   = remote.lastPaintjob != entity->paintjob;
        const bool parts   = remote.lastUpgrades != entity->upgrades;

        if (!colours && !paint && !parts) {
            return;
        }

        if (colours) {
            SDK::SetVehicleRgbColors(remote.handle, entity->colours.data(), Shared::kVehicleColorCount);
            remote.lastColours = entity->colours;
            remote.haveColours = true;
        }

        if (paint || parts) {
            SDK::ApplyVehicleCosmetics(remote.handle, entity->paintjob, entity->upgrades.data(), Shared::kUpgradeSlots);
            remote.lastPaintjob  = entity->paintjob;
            remote.lastUpgrades  = entity->upgrades;
        }
    }

    void ApplyScriptState(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
        if (!remote.handle) {
            return;
        }

        if (remote.haveScriptState && remote.lastEngineOn == entity->engineOn && remote.lastSirenOn == entity->sirenOn &&
            remote.lastOverrideLights == entity->overrideLights && remote.lastLocked == entity->locked) {
            return;
        }

        SDK::ApplyVehicleScriptState(remote.handle, entity->engineOn, entity->sirenOn, entity->overrideLights, entity->locked);
        remote.lastEngineOn       = entity->engineOn;
        remote.lastSirenOn        = entity->sirenOn;
        remote.lastOverrideLights = entity->overrideLights;
        remote.lastLocked         = entity->locked;
        remote.haveScriptState    = true;
    }

    void ApplyExtras(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
        if (!remote.handle) {
            return;
        }

        if (remote.haveExtras && remote.lastDirt == entity->dirtLevel && remote.lastTaxiLight == entity->taxiLight &&
            remote.lastHeadlight == entity->headlightColor && remote.lastFrozen == entity->frozen && remote.lastDamageProof == entity->damageProof &&
            remote.lastCollisions == entity->collisions && remote.lastLandingGear == entity->landingGearDown && remote.lastLightsOn == entity->lightsOn &&
            remote.lastEngineBroken == entity->engineBroken && remote.lastWheelsCanBurst == entity->wheelsCanBurst &&
            remote.lastCanBeDamaged == entity->canBeDamaged && remote.lastNitroCount == entity->nitroCount && remote.lastPlateText == entity->plateText) {
            return;
        }

        SDK::ApplyVehicleExtras(remote.handle, entity->dirtLevel, entity->taxiLight, entity->headlightColor, entity->frozen, entity->damageProof,
                                entity->collisions, entity->landingGearDown, entity->lightsOn, entity->engineBroken, entity->wheelsCanBurst,
                                entity->canBeDamaged, entity->nitroCount);
        SDK::SetVehiclePlateText(remote.handle, entity->plateText);
        remote.lastDirt           = entity->dirtLevel;
        remote.lastTaxiLight      = entity->taxiLight;
        remote.lastHeadlight      = entity->headlightColor;
        remote.lastFrozen         = entity->frozen;
        remote.lastDamageProof    = entity->damageProof;
        remote.lastCollisions     = entity->collisions;
        remote.lastLandingGear    = entity->landingGearDown;
        remote.lastLightsOn       = entity->lightsOn;
        remote.lastEngineBroken   = entity->engineBroken;
        remote.lastWheelsCanBurst = entity->wheelsCanBurst;
        remote.lastCanBeDamaged   = entity->canBeDamaged;
        remote.lastNitroCount     = entity->nitroCount;
        remote.lastPlateText      = entity->plateText;
        remote.haveExtras         = true;
    }

    void EnsureSpawned(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
        if (remote.handle) {
            return;
        }

        const int model = static_cast<int>(entity->modelId);

        if (!SDK::IsVehicleModel(model)) {
            return;
        }

        if (!SDK::IsModelLoaded(model)) {
            SDK::RequestModel(model);
            return;
        }

        const float heading = glm::roll(entity->rotation);

        remote.handle = SDK::CreateScriptVehicle(model, entity->position.x, entity->position.y, entity->position.z, heading, entity->primaryColour,
                                                 entity->secondaryColour, entity->variant, entity->variant2);

        if (!remote.handle) {
            Framework::Logging::GetLogger("Vehicle")->warn("Could not create vehicle {} (model {})", static_cast<std::uint64_t>(entity->GetNetworkID()), model);
            return;
        }

        gVehicleIdByHandle[remote.handle] = static_cast<std::uint64_t>(entity->GetNetworkID());

        remote.lastVariant  = entity->variant;
        remote.lastVariant2 = entity->variant2;
        remote.haveVariants = true;

        SDK::SetVehiclePose(remote.handle, entity->position.x, entity->position.y, entity->position.z, entity->rotation);

        auto state = SDK::ReadVehicle(remote.handle);

        state.health   = entity->health;
        state.engineOn = entity->engineOn;
        state.sirenOn  = entity->sirenOn;
        state.vx       = entity->velocity.x;
        state.vy       = entity->velocity.y;
        state.vz       = entity->velocity.z;
        state.avx      = entity->angularVelocity.x;
        state.avy      = entity->angularVelocity.y;
        state.avz      = entity->angularVelocity.z;

        SDK::ApplyVehicleMotion(remote.handle, state);
        SDK::ApplyVehicleDamage(remote.handle, entity->damageEngine, entity->doorsPacked, entity->wheelsPacked, entity->panels, entity->lights);
        SDK::ApplyVehicleDoorRatios(remote.handle, entity->doorRatiosPacked, SDK::OccupiedDoorMask(remote.handle));
        SDK::BindRemoteVehicleProcessControl(remote.handle);

        if (entity->IsBlown()) {
            // Already a wreck on the wire. Hide the boom — it happened
            // before this client existed.
            SDK::WreckVehicle(remote.handle, true);
            remote.wrecked = true;
        }

        SDK::KeepScriptVehicle(remote.handle);
        SDK::SyncVehicleSimulationStatus(remote.handle);
        SDK::SetVehicleInterior(remote.handle, entity->interior);
        ApplyCosmetics(entity, remote);
        ApplyScriptState(entity, remote);
        ApplyExtras(entity, remote);

        remote.velocity = entity->velocity;
        remote.angular  = entity->angularVelocity;
        remote.health   = entity->health;
        remote.engineOn = entity->engineOn;
        remote.sirenOn  = entity->sirenOn;
        remote.haveDamage = true;
        remote.lastEngine = entity->damageEngine;
        remote.lastDoors  = entity->doorsPacked;
        remote.lastWheels = entity->wheelsPacked;
        remote.lastPanels = entity->panels;
        remote.lastLights = entity->lights;
        remote.lastRatios       = entity->doorRatiosPacked;
        remote.lastEntityRatios = entity->doorRatiosPacked;
        remote.heldRatios       = entity->doorRatiosPacked;
    }

    void DestroyHandle(RemoteVehicle &remote) {
        if (!remote.handle) {
            return;
        }

        gVehicleIdByHandle.erase(remote.handle);

        SDK::CancelEntriesInto(remote.handle);
        SDK::ClearVehicleRgbColors(remote.handle);
        SDK::ClearVehicleHeadlightColor(remote.handle);
        SDK::DestroyScriptVehicle(remote.handle);
        remote.handle = nullptr;
    }

    void RecreateIfVariantChanged(Shared::VehicleEntity *entity, RemoteVehicle &remote) {
        if (!remote.handle || !remote.haveVariants) {
            return;
        }

        if (remote.lastVariant == entity->variant && remote.lastVariant2 == entity->variant2) {
            return;
        }

        // Extras are chosen at CreateCarForScript. Changing them means
        // a new clump — same as MTA SetVariant → ReCreate.
        DestroyHandle(remote);
        remote.haveExtras       = false;
        remote.haveScriptState  = false;
        remote.haveColours      = false;
        remote.haveVariants     = false;
        remote.haveDamage       = false;
        remote.wrecked          = false;
    }
} // namespace VantaMP::Game::VehicleSyncState
