# Vehicle protocol migration table

Every responsibility the legacy vehicle networking held, and what now owns it.
Nothing in the left column survives; nothing in it disappears silently either.

The legacy protocol was **not** a set of separate packet structs — that is why
this table is field-shaped rather than struct-shaped. It was one replicated
snapshot, `VehicleEntity::SerializeFields`, carrying 60 fields on the state
channel plus a raw pose on the transform channel, with one genuine RPC
(`VehicleDamageReport`) beside it.

## Transport

| Old | New |
|---|---|
| `NetworkEntity::SerializeTransform` (base) writing pos/vel/rot every tick for a driven vehicle | `VehicleDriverSync` (quantized, masked, Unreliable). The transform channel is suppressed by a presence bit while a driver stream is live. |
| Transform channel for an unoccupied vehicle | `VehicleUnoccupiedSync`, batched at 400 ms by the server-elected syncer |
| VDS delta on the state channel for **motion** fields | the change mask inside the new packets |
| VDS delta on the state channel for **configuration** fields | **unchanged, and deliberately so** — see "What stays on the replica" |

## Motion and control fields

These were in `SerializeFields` and are now carried by `VehicleDriverSync` /
`VehicleUnoccupiedSync`. They are **removed** from the replica's per-tick
serializer.

| Old field (replica) | New owner |
|---|---|
| `angularVelocity` | `DriverField::kTurnSpeed` / `UnoccupiedField::kTurnSpeed` |
| `health` | `DriverField::kHealth` / `UnoccupiedField::kHealth` / `DamageField::kHealth` |
| `steer`, `accelerator`, `brake`, `gasPedal`, `brakePedal` | `DriverField::kControls` |
| `handbrake` | `VehicleFlag::kHandbrake` |
| `engineOn`, `sirenOn`, `lightsOn`, `onGround` | `VehicleFlag::*` |
| base `position`, `velocity`, `rotation` (transform channel) | `DriverField::kPosition/kRotation/kVelocity` |

## Damage

| Old | New |
|---|---|
| `VehicleDamageReport` with all seven groups sent unconditionally, 27 B of them every report | `VehicleDamageReport` with a 7-bit change mask; only changed groups serialize |
| `applyRatios` bool — a one-field workaround for "status-only reports must not zero the swing" | `DamageField::kDoorRatios`. The mask generalises exactly what that flag hacked around, so the flag is **deleted**. |
| `damageEngine`, `doorsPacked`, `wheelsPacked`, `panels`, `lights`, `doorRatiosPacked` on the replica **and** in the RPC | the RPC only. The replica keeps them as *authoritative server state* for late joiners, but no longer re-serializes them per tick. |

## Ownership

| Old | New |
|---|---|
| implicit: `ownerGUID` on the replica, last driver keeps it, no syncer concept for unowned vehicles | `VehicleSyncOwner` (start/stop), server-elected nearest in-range player, stop-before-start handoff |
| no compact addressing — every reference was the 64-bit `NetworkID` | `WireId` (12-bit slot + 4-bit generation), minted only by the server |

## Occupancy — unchanged

`PlayerEnterVehicle`, `PlayerExitVehicle`, `PlayerOccupancyAbort` were already
semantic, reliable, correctly-shaped events. They are **kept as-is**. Renaming
them to `VehicleEnter`/`VehicleExit` would be churn with no behavioural change,
and the brief explicitly says not to move files for aesthetics.

## What stays on the replica, and why

This is the part that must not be "cleaned up" by reflex. These fields are
**not** duplicated state — they have no representation in the new packets, and
the replica is the correct owner for them:

- **Cosmetics / configuration**: `modelId`, colours, `paintjob`, `upgrades`,
  `plateText`, `dirtLevel`, `headlightColor`, `variant`, `variant2`, `alpha`
- **Script switches**: `locked`, `frozen`, `damageProof`, `collisions`,
  `engineBroken`, `wheelsCanBurst`, `canBeDamaged`, `nitroCount`,
  `overrideLights`, `taxiLight`, `landingGearDown`
- **World placement**: `interior`, `dimension`
- **Latched state**: `blown`
- **Damage groups**: as authoritative state for construction, not per-tick

They change rarely, they must survive a late join, and the VDS delta already
costs one bit each while unchanged. Moving them to a packet would make them
*more* expensive, not less — an RPC pays ~9-26 B of identifier header that the
replica path does not pay at all.

**`VehicleSpawn`/`VehicleDespawn` are therefore not introduced.** The framework's
`ReplicaManager3` construction/destruction already does exactly this job, with
ordering and ownership handled, and adding packets beside it would be a second
source of truth for entity lifetime. The brief lists them as *potential*
structure; the repository already has the mechanism.

**`VehicleTrailer` is not introduced** either: nothing in VantaMP tows anything
yet, so there is no fact to synchronise. Adding the packet now would be dead
code, which is the thing this migration is removing.

## Removed outright

| Symbol | Why |
|---|---|
| `VehicleDamageReport::applyRatios` | replaced by `DamageField::kDoorRatios` |
| `VehicleDamageReport::FromParameters` | the mask makes an all-fields setter meaningless |
| motion/control fields in `VehicleEntity::SerializeFields` | carried by the new packets |
| `VehicleEntity::SerializeTransform` default behaviour for driven vehicles | suppressed by presence bit |
