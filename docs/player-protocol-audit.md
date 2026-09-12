# Player sync protocol

Phase 2. What was on the wire for a player, measured, and the semantic split
that measurement argued for. The audit came first and drove the migration,
which is now **done** — the "what is sent today" figures below are the BEFORE
state, and the measured results are at the end.

## What was sent before

One replica, `PlayerEntity`, across the framework's two channels — the same
shape the vehicle protocol had before Phase 1.

| | Transform (Unreliable) | State (ReliableOrdered, VDS delta) |
|---|---|---|
| Contents | position, velocity, rotation — raw float32 | 34 fields |
| Walking | 41.0 B/tick | 25.8 B/tick |
| Idle | deduped → 0 B | suppressed → 0 B |

**Walking total: 66.8 B/tick = 668 B/s at 10 Hz.**

Note the idle case is already free, exactly as it was for vehicles — RM3's
memcmp dedupe plus the suppressed state channel. Any "idle player" saving
claimed in a redesign would be imaginary.

## The 34 fields by cadence

Classified by how often each actually changes, which is what should decide
where it lives:

**HOT — 11 fields, 20.4 B, changing nearly every tick while moving**

`leftStickX`, `leftStickY`, `accelerator`, `brake`, `buttons`,
`cameraRotation`, `isDucked`, `isInAir`, `isInWater`, `aimHorizontal`,
`aimVertical`

**EVENT — 10 fields, 33.2 B, on change only**

`health`, `armour`, `weaponType`, `ammoClip`, `ammoTotal`, `isReloading`,
`vehicleNetworkId`, `vehicleSeat`, `oxygen`, `isDrowning`

**RARE — 13 fields, 26.5 B, almost never**

`nickname`, `interior`, `dimension`, `money`, `wantedLevel`, `frozen`,
`collisions`, `fightingStyle`, `walkingStyle`, `alpha`, `onFire`, `maxHealth`,
`hasJetPack`

The VDS delta already means EVENT and RARE cost **one bit each** while
unchanged, so the 33.2 + 26.5 B figures are not a per-tick cost. The real
per-tick load is the 20.4 B of HOT payload plus 35 change-bits plus the 41 B
transform.

## Findings

### 1. Aim is the clearest split candidate

`aimHorizontal`, `aimVertical` and `cameraRotation` are **96 bits of float32**
— nearly half the HOT payload — and they have a different cadence from
movement: they change constantly while aiming and barely at all while simply
running. They also have a different consumer (upper-body IK, not the movement
task).

This is a genuine `PlayerAimSync`: same cadence class as movement while aiming,
near-zero when not.

### 2. Three float32 angles are the single biggest quantization win

All three are angles. At 16 bits over ±π the step is 0.0055°, far below what
the IK shows — the same reasoning that made the vehicle quaternion safe.
**96 bits → 48 bits**, halving the aim cost, before any packet split.

`cameraRotation` must stay paired with the input it is resolved against
(the header comment is explicit that a tick's skew makes a player walk
sideways), so it belongs with movement, not with aim.

### 3. Input is already well-shaped

`accelerator`/`brake` are `uint8`, `buttons` is a `uint16` mask, the three pose
booleans are 1 bit each under VDS. The only oversized members are
`leftStickX/Y` as `int16` where GTA's pad is `-128..127` — **32 bits → 16**.

### 4. `vehicleNetworkId` is 64 bits on a player packet

The same argument as the vehicle hot path: this should use the existing
`WireId`. It is EVENT-class so it costs one bit while unchanged — worth fixing
when the ID work is shared, not urgent on its own.

### 5. Health/armour are float32 for a 0..100 range

12 bits over 0..255 would be exact to 1/16 of a point. EVENT-class, so the
saving is small — but it is free and matches what was done for vehicle health.

### 6. Death is already a reliable event — good

`PlayerDeath` exists as its own RPC and the server acts on it. It is **not**
inferred from health reaching zero in a motion packet. This already satisfies
the brief's requirement; no change needed.

### 7. Spawn is already a reliable event — good

`LocalPlayerSpawn` is its own RPC. Likewise correct.

### 8. Shots and reloads are already separate reliable events — good

`PlayerShot` and `PlayerReload` are their own RPCs, relayed by the server
minus the shooter. The brief asks whether shooting deserves separation from
movement; it already has it.

### 9. `nickname` is re-serialized on every snapshot

Deliberate — the header explains a client that missed construction has no
other way to learn it. Under VDS it costs one bit while unchanged, so this is
already cheap. **Leave it.**

## The split

Following the same rule as vehicles — split on cadence, reliability, authority
and semantics, not on file size:

| Packet | Reliability | Cadence | Contents |
|---|---|---|---|
| `PlayerOnFootSync` | Unreliable | 100 ms while moving | wire id, sequence, position, heading, velocity, stick, pedals, buttons, `cameraRotation`, pose flags (ducked/air/water) |
| `PlayerAimSync` | Unreliable | 100 ms **while aiming only** | wire id, sequence, `aimHorizontal`, `aimVertical` (16-bit each) |
| `PlayerShot` | ReliableOrdered | event | **exists, unchanged** |
| `PlayerReload` | ReliableOrdered | event | **exists, unchanged** |
| `PlayerDeath` | ReliableOrdered | event | **exists, unchanged** |
| `LocalPlayerSpawn` | ReliableOrdered | event | **exists, unchanged** |
| `PlayerEnterVehicle` / `ExitVehicle` / `OccupancyAbort` | ReliableOrdered | event | **exist, unchanged** |
| replica `SerializeFields` | ReliableOrdered delta | on change | everything EVENT and RARE: health, armour, weapon, ammo, script state, world placement, cosmetics |

**Five of the eight already exist and are correctly shaped.** The work is
`PlayerOnFootSync` + `PlayerAimSync`, and trimming the replica to EVENT/RARE
state — structurally identical to what Phase 1 did to vehicles.

### What must NOT move to a packet

The same trap as vehicles: an RPC pays a Huffman-encoded identifier
(~9-26 B) that the replica path does not pay at all. Health, armour, weapon,
ammo, money, wanted level and every script switch change rarely and cost one
bit each under VDS. Moving them to packets would make them **more** expensive.
They stay on the replica.

## Projected saving

| | Now | After |
|---|---|---|
| Transform channel | 41.0 B | replaced by `PlayerOnFootSync` |
| HOT state payload | 20.4 B | moved into the packets |
| Aim (3 × float32) | 12.0 B | 6.0 B quantized, and only while aiming |
| Sticks (2 × int16) | 4.0 B | 2.0 B |

A walking, non-aiming player should land near **35-40 B/tick** against today's
66.8 B — roughly the same **~45%** the vehicle driven case achieved, for the
same reasons. That number is a projection from field sizes, **not a
measurement**; it gets measured the way the vehicle numbers were, against the
real `BitStream`, before any of it is believed.

## Deliberately not proposed

- **`PlayerHealthSync`** — health is EVENT-class and already costs one bit
  while unchanged. A packet would cost 25 B of identifier to save nothing.
- **`PlayerWeaponChange`** — same; `weaponType`/`ammo*` on the replica delta
  already behave as an on-change event with reliable delivery.
- **A second entity-ID system** — `WireId` from the vehicle work is reused, not
  duplicated. That is the brief's explicit requirement and the right call.

## Migrated (2026-09-12)

Built exactly as proposed above:

| Packet | Identifier | Reliability | Measured |
|---|---|---|---|
| `PlayerOnFootSync` | `VMP:PF` | Unreliable | 31 B all-fields, 15 B position-only, 13 B input-only |
| `PlayerAimSync` | `VMP:PA` | Unreliable | 8 B |

Add ~9 B RPC4 header to each.

### Measured before vs after

| Scenario | Before | After | |
|---|---|---|---|
| Walking, not aiming | 668 B/s | 400 B/s | **-40%** |
| Walking + aiming | 668 B/s | 570 B/s | **-15%** |
| Driving (player half) | 410 B/s | 0 B/s | **-100%** |
| Idle | 0 B/s | 0 B/s | already free |
| 10 players walking | 6680 B/s | 4000 B/s | **-40%** |

The aiming case is only -15% because `PlayerAimSync` pays its own ~9 B
identifier header. The split still wins overall because a player who is *not*
aiming — the common case outside a firefight — now sends nothing for aim at
all, where before those two float32 angles rode every movement update.

### Legacy removed

`PlayerEntity::SerializeFields` no longer carries `leftStickX/Y`,
`accelerator`, `brake`, `buttons`, `cameraRotation`, `isDucked`, `isInAir`,
`isInWater`, `aimHorizontal` or `aimVertical`. They are seeded in
`OnSerializeConstruction` (once per receiver) so a late joiner still sees a
still player correctly, and carried per-tick only by the packets.

`SerializeTransform` is overridden with the same presence-bit suppression as
`VehicleEntity`, so the pose does not travel twice.

The temporary `SyncDbg` telemetry (`Telemetry`, `gTelemetryBySlot`,
`ReportTelemetry`, the per-tick SEND/RECV logging) is **deleted**.

### Shared, not duplicated

`WireId`, `WireRegistry`, `IsNewerSequence` and the metrics counters moved to
`shared/rpc/common/` and are used by both protocols. Players and vehicles keep
**separate registry instances** — a slot number means nothing across kinds, and
packets are already typed by their RPC identifier.

The wire-id announcement reuses `VehicleSyncOwner` rather than adding a
near-identical player packet: it already carries "this entity has this wire
id", and nothing in it is vehicle-specific. The client routes it by resolving
the entity type.
