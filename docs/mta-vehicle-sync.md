# How MTA synchronises vehicles

An audit of the MTA:SA reference tree (`reference/mtasa-neon`), written so we
stop re-deriving it. Every claim here has a file and line behind it.

The short answer to "is it split by category?": **no — it is split by
*who is driving*, not by what the vehicle is.** Vehicle *type* only ever
selects capabilities (does it have doors, a turret, landing gear), never the
sync channel. That distinction is the thing worth internalising.

## 1. Three channels, chosen by occupancy

MTA has exactly three ways a vehicle's state reaches other clients. Which one
runs is decided by whether the vehicle has a driver, and if so, who.

| Channel | When | Sender | Rate |
|---------|------|--------|------|
| **Vehicle puresync** | Vehicle has a driver | That driver's client | `iPureSync` = 100 ms |
| **Unoccupied vehicle sync** | No driver | An elected *syncer* — a nearby player | `iUnoccupiedVehicle` = 400 ms |
| **Lightsync** | Distant players | Server-relayed, coarse | `iLightSync` = 1500 ms |

`CNetAPI::DoPulse` picks between the first two
(`Client/mods/deathmatch/logic/CNetAPI.cpp:326`): if the local player occupies
a vehicle, it writes vehicle puresync; otherwise player puresync. A passenger
still sends *player* puresync — the vehicle half is gated on seat 0.

### Keysync is a fourth, separate channel

Controller state is **not** carried only in puresync. `IsSmallKeySyncNeeded`
(`CNetAPI.cpp:462`) fires an extra packet the moment a *button* changes, so
input edges are not quantised to the 100 ms puresync grid. It compares
`LeftShoulder1`, `RightShoulder1`, `ButtonSquare`, `ButtonCross`,
`ButtonCircle`, `ButtonTriangle`, `ShockButtonL`, `m_bPedWalk`, and the
sticks with a dead zone of 32.

Planes and helis additionally sync `LeftShoulder2` / `RightShoulder2`
(`CNetAPI.cpp:473`) — that is a *capability* branch, not a channel branch.

## 2. The driver is the authority, and `uiSeat == 0` is how it is enforced

This is the single most repeated pattern in the whole system. Both the write
and the read side gate every vehicle field behind seat 0:

- `WriteVehiclePuresync` — `CNetAPI.cpp:1664`, `1813`
- `ReadVehiclePuresync` — `CNetAPI.cpp:1355`, `1476`, `1577`
- `flags.data.bSyncingVehicle = (pVehicle && seat == 0)` — `CNetAPI.cpp:750`

Rotation, move speed, turn speed, health, sirens, landing gear, the
vehicle-specific block and the damage/parts block are **all** inside those
gates. A passenger transmits their own ped state and nothing about the car.

## 3. Unoccupied vehicles: an elected syncer

When nobody is driving, the **server** picks one client to own the vehicle
(`Server/.../CUnoccupiedVehicleSync.cpp:95-190`), in this priority:

1. The controller (driver, or the driver of a tower) if there is one and they
   are not mid enter/exit
2. Otherwise the first occupant — **a passenger can be the syncer of a
   driverless car**
3. Otherwise the nearest player within `iUnoccupiedVehicleSyncerDistance`
   (130 m)

Syncing stops when the syncer leaves that radius or changes dimension, and a
new one is elected. Note this is *ownership of an empty car*, which is a
different thing from driver authority — a passenger may sync a car nobody is
driving, but may never send driver input.

## 4. Interpolation: error compensation, never a lerp to target

`CClientVehicle::Interpolate` (`CClientVehicle.cpp:3846`) runs only when the
vehicle is remote:

```
if ((m_pDriver && !m_pDriver->IsLocalPlayer() && !IsSyncing()) ||
    (!m_pDriver && !IsSyncing()))
    { UpdateTargetPosition(); UpdateTargetRotation(); }
else
    { RemoveTargetPosition(); RemoveTargetRotation(); }
```

Both updaters add a *share of the remaining error* to wherever GTA has
already put the entity — they never assign an absolute pose:

- Position: `vecNewPosition = vecCurrentPosition + vecCompensation`, alpha
  clamped to **1.5** (`CClientVehicle.cpp:4026`)
- Rotation: `SetRotationDegrees(vecCurrentRotation + vecCompensation)`, alpha
  clamped to **1.0** (`CClientVehicle.cpp:4125`)

Error is scaled on arrival by how late the packet was:
`Lerp(0.25, unlerp(100, delay, 400), 1)` for position and `Lerp(0.40, ...)`
for rotation. Both stop once paid off (`ulFinishTime = 0`), and **rotation
stops independently of position** — this is why a converged remote vehicle
receives position updates but no orientation writes.

Warp threshold: `(15 + 10 * speed) * gameSpeed * TICK_RATE / 100`. Exceeding
it aborts interpolation and snaps to target, including rotation.

## 5. Momentum is per packet, not per frame

`SetMoveSpeed` / `SetTurnSpeed` are called exactly once, where the puresync
packet is read (`CNetAPI.cpp:1420-1421`), and then GTA owns the vehicle until
the next packet. There is no per-frame momentum write anywhere in MTA.

## 6. What *is* per frame

Only two things, both in `StreamedInPulse`:

- `SetUsesCollision(m_bIsCollisionEnabled)` — with the comment *"Vehicles have
  a way of getting their cols back, so we have to force this each frame"*
  (`CClientVehicle.cpp:2405`)
- Door ratio readback, and landing gear for pilotless planes

## 7. Vehicle type selects capabilities only

`CClientVehicleManager::GetVehicleType` (`CClientVehicleManager.cpp:460`) maps
a model to CAR / BIKE / BMX / QUADBIKE / MONSTERTRUCK / PLANE / HELI / BOAT /
TRAIN / TRAILER. That type is then used **only** for capability questions:

| Question | Bikes/BMX |
|----------|-----------|
| `HasDamageModel` | **no** — damage manager is CAR/QUAD/MONSTER/HELI/PLANE/TRAILER only |
| `HasDoors` | no (follows HasDamageModel) |
| Wheel status | `SetBikeWheelStatus` writes +0x65C/+0x65D directly instead |
| `PlaceProperlyOnGround` | `PlaceBikeOnRoadProperly` instead of the automobile one |
| Turret / landing gear / rotors | no |

**There is no bike-specific transform, interpolation, or physics path.** A
bike is synced exactly like a car.

Verified by absence: grepping the whole client tree for `LeanMatrix`,
`m_fLeanLR` or `SetBikeLean` returns **nothing** — MTA never writes bike lean.
`SetVehicleWheelRotation` exists (`CVehicleSA.cpp:721`, and it does handle
BIKE/BMX via `m_afWheelRotationX`) but has **no callers** outside the Lua
binding: wheel rotation is never synced either.

Both are GTA outputs, recomputed from the replayed controller state plus the
per-packet momentum. Anything that writes them from the network is fighting
the game.

## 8. Ped and vehicle stay separated

- A ped's position is never written while it occupies a vehicle
  (`CClientPed::SetPosition`, `CClientPed.cpp:1070`)
- Ped collision is script state only; MTA never clears it for sitting down
  (`CClientPed::SetUsesCollision`)
- A remote rider gets `SetCanBeKnockedOffBike(false)` on warp-in and again on
  every stream-in (`CClientPed.cpp:7443`, `5802`)

## 9. Entity bookkeeping around a matrix write

`CEntitySA::SetMatrix` (`CEntitySA.cpp:343`) is a discrete operation and does
all of this together:

1. `IsValidMatrix` guard (NaN / range)
2. writes the four vectors
3. `m_transform.m_translate = matrix->vPos`
4. `CWorld::Remove` → `UpdateRwMatrix` → `UpdateRwFrame` → `CWorld::Add`

The per-frame path is **not** this. `CEntitySA::SetPosition`
(`CEntitySA.cpp:124`) writes `matrix->vPos` and stops — no RW update, no world
re-bucketing. Conflating the two is a real hazard: `UpdateRwFrame` rebuilds
the frame hierarchy the wheels hang off.

## 10. Tick rates (`Shared/mods/deathmatch/logic/CTickRateSettings.h`)

```
iPureSync                        100
iKeySyncRotation                 100
iKeySyncAnalogMove               100
iNearListUpdate                  100
iPedSyncerDistance               100
iCamSync                         500
iObjectSync                      500
iPedSync                         400
iUnoccupiedVehicle               400
iLightSync                      1500
iPedFarSync                     2000
iUnoccupiedVehicleSyncerDistance 130
iVehicleContactSyncRadius         30
```

## 11. Wire format — everything is quantised

Nothing goes on the wire as a raw float unless it has to
(`Shared/sdk/net/SyncStructures.h`).

| Field | Format | Notes |
|-------|--------|-------|
| Position | `SPositionComponentSync` for X/Y + raw float Z | Range-checked against `SYNC_POSITION_LIMIT` on read; out-of-range is a **rejected packet**, not a clamp |
| Rotation | **3 x uint16 Euler degrees**, `value * 360/65536` | ~0.0055 deg. Vehicle puresync uses the quantised form (`CNetAPI.cpp:1667`), not floats |
| Velocity / turn speed | 1 bit "is zero", else float magnitude + `WriteNormVector` | A stationary vehicle costs **one bit** |
| Health | `SFloatAsBitsSync<12>` | 12 bits, not a float |
| Door ratio | 1 bit "uncompressed", 1 bit "non-zero", else compressed | Zero and fully-open are near-free |

Two things worth noting against our design:

- MTA syncs rotation as **Euler degrees**, we use a quaternion. Ours is
  arguably better behaved (no gimbal issues, no wrap handling) — but it means
  the MTA interpolation constants were tuned against a different
  representation.
- MTA **rejects** a packet whose position fails the range check. We have no
  equivalent validation anywhere on the vehicle path.

## 12. Damage is delta-synced, per part

`SVehicleDamageSync` carries four independent part groups, each a
`SVehiclePartStateSync<COUNT, BITS>` with a **changed flag** so an unchanged
group costs one bit:

| Group | Count | Bits per part |
|-------|-------|---------------|
| Doors | `MAX_DOORS` | 3 |
| Wheels | `MAX_WHEELS` | 2 |
| Panels | `MAX_PANELS` | 2 |
| Lights | `MAX_LIGHTS` | 2 |

There is a second encoding (`SVehicleDamageSyncMethodeB`) used elsewhere in
the protocol. We pack doors/wheels/panels/lights into fixed integers and send
them every tick regardless of change.

## 13. The vehicle-specific block is capability-gated

`WriteFullVehicleSpecific` (`CNetAPI.cpp:2005`) writes **only what the model
has**:

- turret rotation, if `HasTurret`
- adjustable property (e.g. Dumper arm), if `HasAdjustableProperty`
- door open ratios, if `HasDoors` — and only **doors 2..5**, the four
  passenger doors. Hood and boot (0, 1) are *not* in puresync.

A bike therefore sends none of this block. We send six packed door ratios for
every vehicle including bikes.

## 14. Trailer / train chains ride the driver's packet

The towing vehicle's puresync walks the chain
(`CNetAPI.cpp:1694`), writing per link: a continue bit, the element id,
position and rotation, terminated by a zero bit. Trains use
`GetNextTrainCarriage`, everything else `GetRealTowedVehicle`.

The receiver only applies a trailer's transform when it is **not streamed
in** — a streamed-in trailer is left to GTA's own tow physics.

## 15. Server-side validation

The server does not blindly relay. `CVehiclePuresyncPacket::Read`
(`Server/.../CVehiclePuresyncPacket.cpp:50`) enforces:

**Time context** — every element carries `m_ucSyncTimeContext`
(`CElement.cpp:1300`). `GenerateSyncTimeContext()` bumps it on any
server-authoritative move (setElementPosition, spawn, warp). A puresync packet
whose context does not match is **dropped**:

```
return (m_ucSyncTimeContext == ucRemote || ucRemote == 0 || m_ucSyncTimeContext == 0);
```

That is what stops an in-flight client packet from undoing a server teleport.
We have nothing equivalent — a `ForceState` can be silently reverted by a
packet already on the wire.

**Frozen vehicles** — if the vehicle is frozen server-side, state is rejected,
and a "significant" attempt triggers a resync back to the offender. The
thresholds are worth copying if we ever add freeze:

| Quantity | Threshold |
|----------|-----------|
| Position | 0.5 m |
| Rotation | 3 deg |
| Velocity | 0.03 |
| Turn speed | 0.05 |
| Health increase | +0.5 |

**Health** — a client reporting *more* health than the server has fires a Lua
event and can be rejected. Health can go down freely, up only with consent.

**Teleport alert** — a jump beyond `playerTeleportAlert` (100 m) raises a
scriptable event rather than being accepted silently.

## 16. Contact / attached peds

`iVehicleContactSyncRadius` (30 m) governs peds standing on or in contact with
a moving vehicle; `CPlayerPuresyncPacket.cpp:99` uses it to decide the relay
set. MTA moves contact players with the vehicle during interpolation
(`CClientVehicle.cpp:4090`, the `m_Contacts` loop) so someone standing on a
flatbed does not slide off.

We have no contact concept at all.

## 17. Enter / exit is a request-confirm protocol, not a broadcast

One packet id (`PACKET_ID_VEHICLE_INOUT`) carries a 4-bit action. Nine
client to server actions (`CClientGame.h:172`):

```
VEHICLE_REQUEST_IN        VEHICLE_NOTIFY_IN        VEHICLE_NOTIFY_IN_ABORT
VEHICLE_REQUEST_OUT       VEHICLE_NOTIFY_OUT       VEHICLE_NOTIFY_OUT_ABORT
VEHICLE_NOTIFY_JACK       VEHICLE_NOTIFY_JACK_ABORT
VEHICLE_NOTIFY_FELL_OFF
```

and ten server to client returns, including `VEHICLE_REQUEST_IN_CONFIRMED`,
`VEHICLE_REQUEST_JACK_CONFIRMED` and `VEHICLE_ATTEMPT_FAILED`.

The shape is **request -> server decides -> confirm or fail -> client acts ->
notify -> broadcast**. A client never seats itself and then tells everyone; it
asks first. `VEHICLE_ATTEMPT_FAILED` is a real, handled outcome.

Ours is a two-message design (`PlayerEnterVehicle` / `PlayerExitVehicle`) where
the client acts and reports. That is closer to `NOTIFY` without the `REQUEST`
half, which is why we needed `PlayerOccupancyAbort` bolted on to undo it.

### Server-side enter validation (`CGame.cpp:3477`)

Checked before any confirm:

| Check | Detail |
|-------|--------|
| Spawned | `pPed->IsSpawned()` |
| Not a trailer | `IsTrailer(model)` refused |
| Not already busy | `GetVehicleAction() == VEHICLEACTION_NONE` |
| Not already seated | `!pPed->GetOccupiedVehicle()` |
| **Distance** | **50 m** normally |
| **Distance (jack)** | **10 m** if seat 0 is occupied |
| **Distance (water)** | **10 m** + warp for Skimmer / Seasparrow / Leviathan / Vortex when in water, and for the RC Baron |

We do a passenger-reach check client-side (20 units) and nothing server-side.

## 18. Falling off a bike is its own protocol message

`VEHICLE_NOTIFY_FELL_OFF` (`CGame.cpp:3984`) exists because a bike can eject
its rider without any exit task running. The server vacates the seat, fires
`onPlayerVehicleExit` with `jacker=false, forcedByScript=false`, and — notably
— **hands unoccupied-syncer ownership to the player who just fell off**
(`OverrideSyncer`).

Client detection is deliberately crude (`CClientPed.cpp:10490`): "I believe I
occupy a vehicle, GTA says I am not in one, and I am not waiting on a warp".
MTA's own comment concedes it is unreliable:

```
// Jax: this happens when we try to warp into a streamed out vehicle ...
// ..maybe we need a different way to detect bike falls?
```

Until the confirm returns, the client sets `m_bNoNewVehicleTask` so it cannot
enter anything else. We have no equivalent state at all — a rider thrown off a
bike locally stays "seated" as far as our replica is concerned.

## 19. NaN / bad-state recovery on every position read

`CEntitySA::GetPosition` (`CEntitySA.cpp:288`) validates on **read**, not just
on write:

```
if (!IsValidPosition(*pPosition)) { RestoreLastGoodPhysicsState(); ... }
m_LastGoodPosition = *pPosition;
```

`IsValidPosition` rejects NaN and anything outside +/-16000 (X/Y), -5000..100000
(Z). `RestoreLastGoodPhysicsState` rewinds to the last good position, and if
even that is corrupt, dumps the entity at `(6000, rand()%2000, 1000)` rather
than letting a NaN propagate.

`SetPosition` and `SetMatrix` both refuse NaN/Inf input outright.

The interpolation warp check is deliberately inverted for the same reason
(`CClientVehicle.cpp:4034`):

```
// Comparing with NaNs always results to false
if (!((vecCurrentPosition - vecTarget).Length() <= fThreshold))
```

so a NaN takes the *warp* branch and gets snapped to target instead of being
interpolated into more NaNs.

We validate nothing. A single bad float on the wire propagates into the matrix
and stays there.

## 20. Model change resets state by capability

`SetModelBlocking` (`CClientVehicle.cpp:1125`) does not blindly keep damage
across a model swap. It resets wheel and door states when the new model has no
doors, **or when the vehicle type changes**, and only preserves them when both
models have doors and are the same type. Upgrades are dropped outright as
possibly-incompatible. A train becoming a non-train unlinks its carriages.

We recreate the handle on a variant change and keep packed damage bytes
regardless of what the model became.

## 21. Waiting for the ground to load

The single largest piece of MTA machinery we have no equivalent of. A vehicle
whose collision has not streamed in yet is **pinned in place with collision
disabled** rather than allowed to fall.

Per-frame, inside `StreamedInPulse` (`CClientVehicle.cpp:2412`):

```
if (m_bIsFrozen) {
    SetMatrix(m_matFrozen); SetMoveSpeed(0); SetTurnSpeed(0);
} else {
    if (m_pDriver || HasCollisionBeenLoaded(&vecPos))
        GetMatrix(&m_matFrozen);          // remember a good pose
    else {
        SetMatrix(&m_matFrozen);          // pin it back
        SetMoveSpeed(0);
        SetUsesCollision(false);          // and let nothing touch it
    }
}
```

Note the `m_pDriver ||` — **a driven vehicle skips the check entirely**. Only
driverless vehicles get pinned. `m_matFrozen` is continuously updated to the
last pose taken while the ground *was* loaded, so the pin is to somewhere
sane.

`HandleWaitingForGroundToLoad` (`CClientVehicle.cpp:4608`) is the release
logic, and it is deliberately forgiving:

- If there are no MTA objects within a 5 m sphere 3 m below, stop waiting
  immediately — nothing to wait for
- Zero move/turn speed, force `LoadAllRequestedModels`
- Check objects loaded within a radius that **shrinks each frame**
  (`50 * (1 - m_fObjectsAroundTolerance)`, tolerance +0.01/frame)
- Then a ground-distance probe with a tolerance that also grows each frame:
  release when `fDist * (1 - tolerance)` lands in `(-0.2, 1.5)`
- Hard stop after ~3 frames when async loading is off

Both tolerances ratchet so the vehicle is *always* eventually released — it
can never be pinned forever.

We have nothing here. A remote vehicle spawned over unloaded collision falls.

## 22. Handling is synced, once, at entity-add

`SVehicleHandlingSync` (`SyncStructures.h:1981`) is written in
`CEntityAddPacket` (`Server/.../CEntityAddPacket.cpp:608`) and by the handling
RPCs — **not** per tick. It carries the full physics profile:

mass, turn mass, drag, **centre of mass (x/y/z)**, percent submerged, traction
multiplier / loss / bias, drive and engine type, gear count, engine
acceleration and inertia, max velocity, brake deceleration and bias, ABS,
steering lock, **suspension force / damping / high-speed damping / upper and
lower limit / front-rear bias / anti-dive**, collision damage multiplier,
model and handling flags, seat offset, anim group.

`ApplyHandling` then calls `RecalculateHandling`, and for BIKE/BMX also
`RecalculateBikeHandling` (`CClientVehicle.cpp:4535`).

### Bike handling has its own table — and a no-rider centre of mass

`tBikeHandlingData` (plugin-sdk `tBikeHandlingData.h`, MTA
`CBikeHandlingEntrySA.h`):

```
fLeanFwdCOM   fLeanFwdForce   fLeanBackCOM   fLeanBackForce
fMaxLean      fFullAnimLean   fDesLean
fSpeedSteer   fSlipSteer
fNoPlayerCOMz            <-- centre of mass Z when there is NO rider
fWheelieAng   fStoppieAng  fWheelieSteer
fWheelieStabMult   fStoppieStabMult
```

`fMaxLean`, `fDesLean` and `fFullAnimLean` are where bike lean actually comes
from — handling data, not sync. And **`fNoPlayerCOMz` (offset 0x28) means GTA
uses a different, higher centre of mass for a riderless bike**, which is
precisely what makes an empty bike fall over.

That is worth holding onto: if a remote bike's rider is not registered in
`m_pDriver` as far as GTA is concerned, the game will apply the no-rider
centre of mass and the bike will topple — no matter how correct the
transform sync is.

## 23. Streaming out is a real destroy, with state saved first

`StreamOut` (`CClientVehicle.cpp:2622`) calls `Destroy()` and cancels any
pending model request. `Destroy` first copies live state back into the
wrapper (`CClientVehicle.cpp:3144`):

```
GetMatrix(&m_Matrix);
GetMoveSpeed(&m_vecMoveSpeed);
GetTurnSpeed(&m_vecTurnSpeed);
m_fHealth = GetHealth();
m_fHeliRotorSpeed / m_fPlaneRotorSpeed
```

so the GTA entity is disposable and `CClientVehicle` is the durable record.
`Create()` restores all of it. Our `RemoteVehicle` plays the same role but we
never voluntarily stream a vehicle out — we hold every replicated vehicle
resident for the life of the session.

## 24. The context switch — MTA's version of our pad swap

`SwitchContext` (`Client/multiplayer_sa/multiplayer_keysync.cpp:942`) is the
direct counterpart of our `VehicleProcessControl_Hook` pad swap, and it is
substantially wider than ours.

Each remote ped owns a **whole `CPadSAInterface`**, not a controller state:
`CRemoteDataStorageSA::m_pad` (`CRemoteDataSA.h:54`), with
`CurrentControllerState()` returning `&m_pad.NewState` and
`LastControllerState()` returning `&m_pad.OldState`. Switching in is a single
`MemCpyFast` of the entire pad struct — button history and press timings
included, which is why edge detection survives.

What MTA swaps or suppresses, beyond the pad:

| What | Why |
|------|-----|
| `pLocalPad->Store()` first | The local pad is preserved by the game's own mechanism, not a manual copy |
| `SetHornHistoryValue(cs->ShockButtonL == 255)` | "this is to fix the horn/siren" |
| `SetLastTimeTouched(systemTime)` | "disables the impatient actions on remote players (which cause desync)" — otherwise GTA thinks the player is idle and starts fidget animations |
| `VAR_CameraRotation` = remote's | "this is to make movement work correctly" |
| `SetGravity(data->m_fGravity)` | Per-player gravity |
| Mouselook off (`0xB6EC2E`) | Unless holding a first-person weapon or melee-strafing |
| Night vision / infrared off | Local goggles must not apply to a remote ped |
| Cough-on-teargas NOP'd | Local reaction code must not fire remotely |
| `ClearWeaponTarget` returns early | Prevented for remote players |
| Weapon click sounds suppressed | So remote actions are not heard as local ones |

We swap the pad and camera rotation, and disable mouselook. We do **not** do
the horn history, the last-touched timestamp, or any of the suppressions.

`SetLastTimeTouched` is the one worth noting: without it GTA decides the
player is idle and plays "impatient" actions — MTA's own comment says this
causes desync.

## 25. Steering angle is never synced, confirmed by absence

Grepping the whole MTA client for `m_fSteerAngle`, `SetSteerAngle` or
`GetSteerAngle` returns **nothing**. It is purely a `ProcessControl` output
derived from the swapped pad, exactly as our own code comments already claim.
That part of our design matches MTA precisely.

## 26. Suspension lines are per-vehicle and size-varying

`GetPrivateSuspensionLines` (`CVehicleSA.cpp:1986`) allocates a private copy
of the collision model's suspension lines, and the size depends on type:

| Type | Bytes |
|------|-------|
| Monster truck | 0x90 |
| **Bike / BMX** | **0x80** |
| Everything else | `numSuspensionLines * 0x20` |

`RecalculateSuspensionLines` then runs **only for cars and monster trucks** —
bikes, boats and trains are excluded, and trains are excluded again by model
id. So a bike's suspension is left entirely to GTA.

Both accessors carry explicit null-guards down the
`modelInfo -> pColModel -> m_data` chain, with the comment that "during
streaming GC races any of these pointers can be transiently null while a
CAutomobile still runs a tick on the entity". That is a real hazard we deref
through unguarded in several places.

## 27. Destruction scrubs dangling references

`CVehicleSA::~CVehicleSA` (`CVehicleSA.cpp:398`) does, in order:

1. `delete[] m_pSuspensionLines`
2. `CVehicle::ExtinguishCarFire` (0x6D2460)
3. `ProjectileInfo::RemoveEntityReferences(this)`
4. `CWorld::Remove`
5. **`CWorld::RemoveReferencesToDeletedObject`** (0x565510)
6. pool removal

We do evacuate, extinguish, release the realtime shadow, clear colours, then
`CWorld::Remove` and pool-free — but **not** steps 3 or 5. Anything still
holding a pointer to the entity (projectiles, targeting, camera, fire) keeps a
dangling reference after we free it.

`RemoveReferencesToDeletedObject` derives cleanly if we ever want it:

```
"53 8B 1D 90 44 B7 00 55 56 57 8B 7B 08 85 FF 74"  // 0x00565510
```

## 28. Vehicle ProcessControl hooks: all ten subclasses, at startup

MTA installs its context switch on **every** vehicle subclass vtable, once, at
init (`multiplayer_keysync.cpp:723`):

```
CPlayerPed  CAutomobile  CMonsterTruck  CTrailer  CQuadBike
CPlane      CBmx         CTrain         CBoat     CBike       CHeli
```

plus `ProcessCollision` and `PreRender` on CPlayerPed/CAutomobile for timing.

Addresses (`multiplayer_keysync.h:64`):

| Subclass | ProcessControl | vtable entry |
|----------|----------------|--------------|
| CAutomobile | 0x6B1880 | 0x871148 |
| CBike | 0x6B9250 | 0x871388 |
| CBmx | 0x6BFA30 | 0x871550 |
| CBoat | 0x6F1770 | 0x8721C8 |
| CMonsterTruck | 0x6C8250 | 0x871800 |
| CTrailer | 0x6CED20 | 0x871C50 |
| CQuadBike | 0x6CDCC0 | 0x871B10 |
| CPlane | 0x6C9260 | 0x871970 |
| CTrain | 0x6F86A0 | 0x872398 |

We instead patch lazily, per spawned vehicle, discovering the vtable from the
live object (`BindRemoteVehicleProcessControl`). Both work; MTA's is
deterministic and covers subclasses no player has spawned yet, ours only ever
touches vtables we have actually seen.

### Our slot constants are correct — verified against the exe

`kProcessControlSlot = 10` and `kBlowUpCarSlot = 41` were checked by walking
the real vtables in `gta-clean/gta_sa.exe` and comparing slot 10 against MTA's
known `ProcessControl` addresses:

| Subclass | vtable base | slot 10 | matches |
|----------|-------------|---------|---------|
| CAutomobile | 0x871120 | 0x6B1880 | yes |
| **CBike** | **0x871360** | **0x6B9250** | **yes** |
| CBmx | 0x871528 | 0x6BFA30 | yes |
| CBoat | 0x8721A0 | 0x6F1770 | yes |

So the hook reaches bikes correctly. **This rules out "the bike's
ProcessControl is never hooked" as an explanation** — a hypothesis worth
discarding explicitly, since it would have produced exactly the observed
symptoms.

## 29. The vehicle context switch does more than the ped one

`SwitchContext(CVehicle*)` (`multiplayer_keysync.cpp:1135`) resolves the
vehicle's **driver** and switches to that ped, then adds vehicle-specific
suppression:

| What | Why (MTA's words) |
|------|-------------------|
| `SetVehicleAudioContext(NUM_LocalVehicleAudioContext)` | "Prevent the game making remote players vehicle's audio behave like locals (and deleting radio etc when they are removed)" |
| NOP + JMP at 0x6AEA25 | "For tanks, to prevent our mouse movement affecting remote tanks" |
| NOP + JMP at 0x729B96 | "Same for firetrucks and SWATs" |
| Restore two CALLs at 0x50237C / 0x5023A3 | Radio hack undone when back in local context |

Note the shape: the vehicle switch is **driver-derived**. There is no such
thing as switching context to a vehicle without a driver — which is
consistent with driver-only authority throughout.

We do the pad and camera swap but none of the audio-context or mouse-movement
suppression, so a remote tank/firetruck can still be turned by the local
mouse.

## 30. Seat index is translated to a GTA door, per vehicle type

`ConvertIndexToGameSeat` (`CClientVehicleManager.cpp:580`) maps a **network
seat index** to a **GTA door constant** — they are not the same numbering, and
the mapping depends on the vehicle.

Door constants (`Client/sdk/game/TaskCar.h:17`), which match ours exactly:

```
DOOR_FRONT_LEFT = 0    DOOR_FRONT_RIGHT = 8
DOOR_REAR_RIGHT = 9    DOOR_REAR_LEFT   = 11
```

The mapping by max passenger count:

| Max passengers | Index 0 | Index 1 | Index 2 | Index 3 |
|----------------|---------|---------|---------|---------|
| 0 or 255 | FRONT_LEFT | invalid | invalid | invalid |
| 1, **bike/quad** | FRONT_LEFT | **REAR_RIGHT** | REAR_LEFT | REAR_RIGHT |
| 1, other | FRONT_LEFT | FRONT_RIGHT | invalid | invalid |
| 3 | FRONT_LEFT | FRONT_RIGHT | ... | ... |

**A bike pillion is seated at `DOOR_REAR_RIGHT` (9), not `DOOR_FRONT_RIGHT`
(8)** — GTA puts bike passengers in a rear slot. MTA passes that translated
constant straight into `CTaskSimpleCarSetPedInAsPassenger`
(`CClientPed.cpp:3429`, `7761`).

We pass `passengerNumber = seat - 1` to GTA's own
`CCarEnterExit::ComputeTargetDoorToEnterAsPassenger` and use whatever it
returns. That is a *different* strategy, not obviously wrong — GTA resolves
the door itself — but it means we never exercise MTA's bike special case, and
our `m_apPassengers[seat - 1]` indexing assumes a linear seat array that the
translation table says is not linear for bikes.

Worth checking against a real bike pillion before changing anything.

Seat validity (`IsValidSeat`, `CClientVehicleManager.cpp:511`) also has rules
we partially share: the Camper caps at seat 2 regardless of its declared
count, and `maxPassengers == 255` means **driver only**, not 255 seats — the
latter we do handle.

## 31. Verified correct against the binary

Several of our constants were checked directly against
`current/build/gta-clean/gta_sa.exe` rather than assumed. All match:

| Ours | Value | Verified against |
|------|-------|------------------|
| `kProcessControlSlot` | 10 | slot 10 of CAutomobile / CBike / CBmx / CBoat vtables resolves to MTA's known `ProcessControl` addresses |
| `kBlowUpCarSlot` | 41 | resolves to per-subclass BlowUpCar |
| `kOpenDoorSlot` | 27 | CAutomobile overrides (0x6A6AE0); bike/BMX/boat share CVehicle's base (0x6B5820) |
| `kDoorNodeIndex` | `{0x10,0x11,0x0A,0x08,0x0B,0x09}` | identical to MTA `s_iCarNodeIndexes` |
| `kPanelNodeIndex` | `{15,14,-1,-1,18,12,13}` | identical to MTA `GetCarNodeIndexFromPanel`, including the two rear-wing gaps |
| Door constants | 0 / 8 / 9 / 11 | identical to MTA `TaskCar.h` |

`kOpenDoorSlot = 27` being the shared `CVehicle` implementation for bikes is
consistent with our code skipping door work for non-automobiles.

## 32. Passenger counts come from a static table, not GTA

`GetMaxPassengerCount` (`CClientVehicleManager.cpp:497`) reads
`g_ucMaxPassengers[model - 400]` — a hand-maintained 212-entry table
(`CClientVehicleManager.cpp:19`) — **not** GTA's `m_nMaxPassengers`.

Decoded for the models that matter here:

| Model | Vehicle | Max passengers |
|-------|---------|----------------|
| 522 | **NRG-500** | **1** |
| 521 | FCR-900 | 1 |
| 523 | HPV1000 (police bike) | 1 |
| 462 | Faggio | 1 |
| 463 | Freeway | 1 |
| 481 | BMX | 0 |
| 509 | Bike | 0 |

So a pillion on an NRG-500 is legal, and per section 30 that pillion belongs
at `DOOR_REAR_RIGHT`.

### 255 means different things to us and to MTA

MTA treats `255` as **driver only**: `IsValidSeat` returns false for any
`ucSeat > 0` when the table says 255.

We treat GTA's `m_nMaxPassengers == 0xFF` as *"unknown, so allow every seat up
to the array bound"* (`occupancy.cpp:667`), on the reasoning that GTA leaves it
0xFF until it has worked the count out.

Both readings are defensible because they are reading **different sources** —
MTA a static table, us a runtime field. Decoding the table shows every model
MTA marks 255 is a trailer or otherwise unenterable:

```
435 450 569 584 590 591 607 608 609 611   (all trailers / Box Freight / Boxville)
```

**No bike is in that set**, so this divergence does not touch the bike case.
It would matter if someone tried to ride a trailer.

## 33. Controlling player walks the tow chain

`GetControllingPlayer` (`CClientVehicle.cpp:1891`) is not just `m_pDriver`. If
there is no driver it falls back to occupant 0, then walks
`GetTowedByVehicle()` upwards, taking the driver of each towing vehicle it
finds. The last one wins.

So a trailer with nobody in it is "controlled" by whoever drives the truck
pulling it. We have no tow concept, so this is moot for us today — but it is
the reason MTA's syncer election (section 3) can say "the driver of a tower"
as a first-class case.

## 34. GTA's own passenger-door chooser, disassembled

Section 30 said MTA seats a bike pillion at `DOOR_REAR_RIGHT` while we let GTA
pick. That is true, but it left open what GTA actually picks. Disassembling
`CCarEnterExit::ComputeTargetDoorToEnterAsPassenger` (0x64F190) from the clean
exe answers it:

```
+0x00  mov edx,[esp+4]                 ; vehicle
+0x04  mov cl,[edx+0x429]              ; vehicle flags
+0x0D  test cl,2
+0x10  jz +0x18
+0x12  mov eax,8 ; ret                 ; flagged models -> DOOR_FRONT_RIGHT
+0x18  mov ecx,[esp+8]                 ; passengerNumber
+0x1F  jz +0x2D                        ; passenger 0
+0x22  jz +0x4A                        ; passenger 1
+0x25  jnz +0x4F                       ; passenger >2 -> 0xFFFFFFFF
+0x27  mov eax,9 ; ret                 ; passenger 2 -> DOOR_REAR_RIGHT
+0x2D  cmp dword [edx+0x590],9         ; m_nVehicleClass == VEHICLE_BIKE
+0x34  jz +0x4A                        ; bike -> DOOR_REAR_LEFT
+0x36  mov eax,[edx+0x384] / [eax+0xCC] / shr 9 / test 1
+0x48  jz +0x12                        ; -> DOOR_FRONT_RIGHT
+0x4A  mov eax,11 ; ret                ; DOOR_REAR_LEFT
```

Verified: `0x590` is `m_nVehicleClass` (SDK `VALIDATE_OFFSET(CVehicle,
m_nVehicleClass, 0x590)`), and `VEHICLE_BIKE == 9` counting from
`VEHICLE_AUTOMOBILE == 0`. **GTA has its own explicit bike branch here.**

### The two schemes index differently

| | Argument | Bike, first passenger |
|---|---|---|
| MTA `ConvertIndexToGameSeat` | **network seat** (0 = driver) | seat 1 -> **9** `DOOR_REAR_RIGHT` |
| GTA `ComputeTargetDoor...` | **passenger number** (0 = first passenger) | passenger 0 -> **11** `DOOR_REAR_LEFT` |

We pass `passengerNumber = seat - 1`, so a bike pillion at network seat 1
becomes passenger 0 and GTA hands back **11**.

So for the same physical seat, MTA uses door 9 and we use door 11. Both are
rear doors and both are "valid"; they are simply different sides. GTA's own
choice is arguably the more authoritative one, since it is the function the
single-player game uses.

**This is a difference, not a proven defect.** It is worth an experiment
before it is worth a change: if a remote bike pillion mounts from the wrong
side or the walk-in path looks wrong, this is the knob. It does not obviously
explain a bike falling over.

Recorded because section 30 raised it as the leading bike-specific lead, and
the disassembly narrows it from "we might be doing the wrong thing" to "we
choose the other rear door".

## 35. CBike::ProcessControl is genuinely driver-gated

Disassembling `CBike::ProcessControl` (0x6B9250) in the clean exe settles
whether a rider matters to bike physics. It reads `m_pDriver` (+0x460) **five
times**, and at least one of those gates a large block:

```
   8b 86 60 04 00 00   mov  eax,[esi+0x460]   ; m_pDriver
   85 c0               test eax,eax
   0f 84 f1 00 00 00   jz   +0xF1             ; NO DRIVER -> skip ~241 bytes
   8b 48 18            mov  ecx,[eax+0x18]
   ...                                        ; anim / task lookup on the rider
```

**This confirms the mechanism behind section 22 in the direction that matters:
a bike with no `m_pDriver` runs materially less physics than one with a
rider.** If our remote rider is not in `m_pDriver` as far as GTA is concerned,
the bike is simulated as an empty bike.

> **Analysis caveat.** `gta_sa.exe` is HOODLUM-packed and has **two sections
> named `.text`**; a lookup that keeps the last match silently produces
> negative file offsets. See section 37. These offsets were re-checked with
> the section pinned and are correct.

### Bike handling fields ProcessControl actually consumes

`m_pBikeHandlingData` is at CBike +0x63C. Six loads of it inside
ProcessControl, with the field each one reaches:

| Site | Insn | Field |
|------|------|-------|
| +0x0256 | `fld [edx+0x2C]` | `fWheelieAng` |
| +0x0314 | `fld [edx+0x30]` | `fStoppieAng` |
| +0x18A4 | `fmul [edx+0x20]` | `fSpeedSteer` |
| +0x18F2 | `fmul [eax+0x24]` | `fSlipSteer` |
| +0x0170 | stores the pointer to `[esi+0xA8]` for later use | - |
| +0x28B5 | `fdivr`, offset resolved later | - |

So ProcessControl itself consumes wheelie/stoppie angles and the two steering
coefficients. **`fMaxLean`, `fDesLean` and `fNoPlayerCOMz` are not read at
these sites** — they are used elsewhere in the bike code (lean matrix
calculation and centre-of-mass setup), which is consistent with lean being
computed outside ProcessControl.

### Limits of this analysis

Static byte inspection can prove *that* a field is read and *which* offset,
but not always *which struct* a register points to without tracing provenance.
The three `fld [reg+0x28]` sites found earlier could not be attributed to
`fNoPlayerCOMz` with confidence — they are `edi`/`eax`-relative and may belong
to another structure entirely.

So: the driver gating above is **proven**. The specific claim that
`fNoPlayerCOMz` is applied to a riderless remote bike remains **plausible but
unproven** by this method. Confirming it needs a runtime read of
`m_pDriver` on a remote bike, which is a two-line probe.

## 36. Who actually writes `m_pDriver`

Scanning the real code section (VA 0x401000, raw 0x400, size 0x455E00) for
`m_pDriver` at CVehicle +0x460:

| Access | Encoding | Count |
|--------|----------|-------|
| read | `8B /r disp32` | **359** |
| write | `89 /r disp32` | **5** |

The five write sites:

```
0x442EA4   0x44324F   0x443503   0x448193   0x6D6045
```

Three of them (0x442EA4, 0x44324F, 0x443503) fall inside
`CGameLogic::Update` (0x442AD0) — respawn and resurrection bookkeeping, not
seating.

Notably, `CTaskSimpleCarSetPedInAsDriver::ProcessPed` (vtable slot 7 of
0x86EE28 -> 0x64B950) **reads** `m_pDriver` six times but never writes it. The
task validates and drives animation; the actual pointer assignment happens
elsewhere, which is consistent with GTA seating peds through
`CCarEnterExit`/`CPed` helpers rather than the task itself.

Practical consequence: the seating path is not a single obvious write we can
point at statically. Confirming that a remote rider lands in `m_pDriver` is
cheaper to do at runtime than to prove from the binary.

### Verified along the way

| Ours | Value | Verified |
|------|-------|----------|
| `kProcessPedSlot` | 7 | `CTaskSimpleCarSetPedInAsDriver` vtable 0x86EE28: slots 0-6 are the base `TaskVTBL` (DeletingDestructor, Clone, GetSubTask, IsSimpleTask, GetTaskType, StopTimer, MakeAbortable), slot 7 is `ProcessPed` -> 0x64B950. Matches MTA's `TaskSimpleVTBL` layout exactly |

## 37. The exe is HOODLUM-packed, and some functions are stubs

Two facts that any future static analysis of `gta-clean/gta_sa.exe` has to
account for.

### Two sections named `.text`

```
.text     VA 0x00401000  vsize 0x456000  raw 0x00000400   <-- real code
_rwcseg   VA 0x00857000
.rdata    VA 0x00858000
.data     VA 0x008A4000
_TEXT_HA  VA 0x00C9E000
...
.text     VA 0x00CB1000  vsize 0x64A000  raw 0x004F3000   <-- HOODLUM
.HOODLUM  VA 0x01556000
```

A section walk that keeps the **last** match on the name `.text` picks
0x00CB1000 and every VA-to-file-offset conversion afterwards is wrong — it
produces negative offsets for real code addresses. Pin the section explicitly:

```
T_VA, T_PR, T_RS = 0x401000, 0x400, 0x455E00
```

### Some functions are replaced by trampolines

`CBike::CalculateLeanMatrix` (0x6B7150) does not start with a prologue:

```
90 e9 a6 a8 d4 ff     nop ; jmp 0x4019FC
```

0x4019FC is inside the HOODLUM dispatcher table (a run of
`64 a1 00 00 00 00 / e9 ...` thunks). The real body has been relocated, so
scanning "the function" at its nominal address finds nothing — which is
exactly what happened when looking for `m_pDriver` and bike-handling reads
inside it, and the zero result was an artefact, not a fact.

Checked across the functions this audit touches:

| Function | Address | State |
|----------|---------|-------|
| `CPhysical::AddToMovingList` | 0x542800 | real code |
| `CWorld::RemoveReferencesToDeletedObject` | 0x565510 | real code |
| `CTaskSimpleCarSetPedInAsDriver::ProcessPed` | 0x64B950 | real code |
| `CCarEnterExit::ComputeTargetDoorToEnterAsPassenger` | 0x64F190 | real code |
| `CAutomobile::ProcessControl` | 0x6B1880 | real code |
| **`CBike::CalculateLeanMatrix`** | **0x6B7150** | **HOODLUM stub** |
| `CBike::ProcessControl` | 0x6B9250 | real code |

So the disassembly in sections 34, 35 and 36 stands — those addresses are all
intact code. **Bike lean specifically cannot be studied statically in this
exe**, because the one function that computes it is the one that was moved.

This also explains a derived signature oddity: `AddToMovingList`'s pattern
`"56 8B F1 90 E9 E1"` contains `90 E9` in its body, which is a relocation
thunk rather than instructions we should read past.

### Re-verified with the section pinned

`CBike::ProcessControl`'s five `m_pDriver` reads, recomputed correctly:

```
VA 0x6B92FA (+0x00AA)   8b 8e 60 04 00 00  3b cf  74 3a     cmp against another ped, jz
VA 0x6B9CCF (+0x0A7F)   8b 86 60 04 00 00  85 c0  0f 84 f1 00 00 00
VA 0x6B9DFB (+0x0BAB)   8b 8e 60 04 00 00  85 c9  74 16
VA 0x6B9E0E (+0x0BBE)   8b 8e 60 04 00 00  e8 ...            call with driver
VA 0x6BB894 (+0x2644)   8b 86 60 04 00 00  85 c0
```

Four of the five are immediately followed by a null test and a conditional
jump. The section-35 conclusion — bike physics is materially driver-gated —
is confirmed against the correct section.

## Reverted: ground pin and under-floor lift

Both were implemented and both caused remote vehicles to jitter, rubber-band
and fly. Recorded so they are not attempted the same way again.

**Ground pin.** The driver bypass used `VehicleHasDriver`, which reads GTA's
`m_pDriver`. For a remote player's car that pointer is not reliably set on the
observing client - which is the open question the `DRIVERCHK` probe exists to
answer. So a *driven* remote car fell through to the collision check, got
pinned to a stale pose, and fought the interpolator every frame.

Any retry needs occupancy from the replica (`entity->vehicleNetworkId` on some
player) rather than from GTA, and needs testing on its own.

**Under-floor lift.** MTA's `UpdateUnderFloorFix` tests the *reported* Z
velocity to mean "the owner is parked on something solid, not falling". Ours
passed `remote.velocity.z`, so a car driving along a slope - near-zero Z
velocity, target legitimately above us - satisfied the condition repeatedly
and got yanked upward.

## Where Vanta differs

Rows marked **done** have since been implemented - this table is a status
board, not a backlog. Anything unmarked is still a real difference.

Recorded as observations, not as a task list.

| Area | MTA | Vanta |
|------|-----|-------|
| Channels | 3 (puresync / unoccupied / lightsync) + keysync | 1 replica at 100 ms |
| Empty-car ownership | Server elects a nearby syncer, re-elected on distance | **done** - `PulseSyncers`, 130 m keep / 110 m adopt, throttled to 250 ms |
| Input cadence | Keysync fires on button edges, off the puresync grid | Buttons ride the 100 ms replica |
| Controller fields | 10 incl. `LeftShoulder1/2`, `RightShoulder2` | **done** - handbrake split from aim, shoulders added |
| Distant vehicles | Lightsync at 1500 ms | Same rate regardless of distance |
| Vehicle type | Drives capability checks | **done** - `shared/vehicle/type.h`, cached per remote vehicle |
| Wire format | Quantised per field, delta damage, zero-bit shortcuts | Full-width fields every tick |
| Rotation | 3 x uint16 Euler degrees | Quaternion |
| Door ratios | Doors 2-5 only, capability-gated | All six, every vehicle |
| Stale packets | Rejected by sync time context | No equivalent; a late packet can undo a ForceState |
| Server validation | Position range, frozen, health-up, teleport alert | None on the vehicle path |
| Trailers | Chain rides the driver's packet | **partly** - towed vehicles now skip the transform; the chain itself is not synced |
| Contact peds | Moved with the vehicle, 30 m radius | still missing |
| Enter/exit | Request -> confirm -> notify, with ATTEMPT_FAILED | Client acts, then reports; abort bolted on |
| Enter distance | Server-checked: 50 m, 10 m for jack/water | Client-side reach only |
| Fell off bike | Own protocol message, hands over syncer | **partly** - `bKnockedOffBike` reports the exit as a warp; no dedicated message |
| NaN handling | Validated on every read and write, with recovery | **done** - `shared/vehicle/validate.h`, refused on send and receive |
| Model change | Resets damage by capability and type | Keeps packed bytes |
| Unloaded ground | Driverless vehicles pinned, collision off, ratcheting release | **tried and reverted** - see below |
| Handling | Full profile synced at entity-add, incl. suspension and COM | Not synced |
| Bike lean source | `fMaxLean` / `fDesLean` in bike handling data, outside ProcessControl | Not synced, not considered |
| Bike physics gating | `CBike::ProcessControl` skips ~241 bytes with no `m_pDriver` | Relies on the same task MTA uses; unverified at runtime |
| Streaming out | Real destroy; state saved into the wrapper | Never streamed out |
| Remote pad | Whole `CPadSAInterface` per ped, swapped wholesale | Same idea, narrower |
| Context switch | Horn history, last-touched, gravity, goggles, sound suppression | Pad + camera + mouselook only |
| Idle fidgets | `SetLastTimeTouched` stops "impatient actions" | Not handled |
| Destroy | Scrubs projectile and world references first | `CWorld::Remove` only |
| Suspension | Private per-vehicle copy, bike-sized 0x80 | Untouched |
| ProcessControl hooks | All 10 subclasses, fixed vtables, at startup | Lazy per spawned vehicle (slots verified correct) |
| Vehicle audio context | Forced non-local for remote vehicles | Not handled |
| Tank / firetruck turrets | Local mouse suppressed via code patches | Not handled |
| Seat -> door | Table: bike pillion -> REAR_RIGHT (9) | GTA's own chooser -> REAR_LEFT (11); both rear, different side |
| Passenger count | Static 212-entry table per model | GTA's runtime `m_nMaxPassengers` |
| Meaning of 255 | Driver only (trailers) | Unknown, allow all seats |
| Controlling player | Falls back through the tow chain | Driver only |
