# Vehicles

A car is three channels that must not be collapsed into one snapshot.

| Channel | Carrier | Who writes it |
|---------|---------|----------------|
| Occupancy | Reliable `PlayerEnterVehicle` / `PlayerExitVehicle` | The client that started the walk; the server stamps the seat |
| Motion | `VehicleEntity` replica, 100 ms | Seat 0, or the last driver after they get out |
| Controls | The same replica's pad fields | The driver. Applied by swapping the pad around `ProcessControl` |

Puresync never seats anyone. A flag that said "entering" would restart the
door animation every tick; an unreliable packet can be missed and leave a
player standing beside a car everyone else sees them driving.

A new stick press or a jump while walking in cancels the enter task —
**GTA does that itself**, and we only report it. `CPlayerPed` ends its own
enter task when the player jumps or steers away; that is what the
`bHasDisplayedPlayerQuitEnterCarHelpText` save flag is the help text for.
We used to abort it a second time from the sync tick, which meant GTA's
unwind and our `MakeAbortable` tore at the same task on the same frame —
visible as a stuttering, half-played step-back.

MTA never intercepts the cancel key at all. It polls `IsEnteringVehicle()`
(`CClientPed.cpp:8272`) and, on the frame the task is gone, asks
`GetRealOccupiedVehicle()` which way it ended: a seat means notify-in, no
seat means notify-abort. We do the same — the local state machine watches
for `!occupancy.entering` and sends `PlayerExitVehicle` with `abortEnter`
so remotes stop walking and scripts do not see a leave.

An abort is **not** an exit on the receiving side either. The ped never
reached a seat, so `ExitVehicle` has nothing to get them out of; it asks the
enter task to unwind instead. That matters because *the unwind is what
closes the door* — deleting the task skips it and leaves the door hanging
open on every other machine.

## Keys

| Key | Meaning |
|-----|---------|
| **F** | Driver enter / exit, and jack. Always seat 0. May walk in through a passenger door |
| **G** | Passenger enter / exit (`enter_passenger`). Never jacks. Never steals the driver |

F is the game's own enter. G is edge-detected in `game/patches/script.cpp`
and handed to `TryLocalPassengerKey`. Skip G while reloading, already
entering, or already leaving. A seated driver ignores G — F is how they get
out.

Passenger reach is 20 units, measured from the **nearest passenger door**,
not the vehicle centre. `m_nMaxPassengers == 255` means unknown, not 255
seats. Never call `GetNearestCarPassengerDoor` if `m_pRwObject` is null.

Enter tasks must stamp a real GTA `eTargetDoor` (`8` / `9` / `11`), not
`0` / `0xFF`. Remotes walk to the side the owner used. Without the door
they path to whichever is nearest from where they spawned and freeze
mid-walk — or sit in the passenger seat when they meant to jack.

## Jacking

MTA's rule, and ours:

1. The jacker sends `PlayerEnterVehicle` seat 0, with the door they walked
   to (which can be the passenger side).
2. If seat 0 is taken and this is not a warp, the server **broadcasts the
   enter and does not assign**. The occupant stays seated. Clients run
   `CTaskComplexEnterCarAsDriver` + `SetTargetDoor`. GTA drags the occupant
   out.
3. When the jacker actually sits, the client sends the same RPC with
   `warp = true`. The server vacates the occupant, assigns the jacker, and
   `SetOwner`s the car.

Do not start `LeaveCar` on an occupant while `IsAnyoneEnteringVehicleAsDriver`
is true — that fights the drag-out.

`AssignSeat` never reassigns a taken seat to a different player. F on an
occupied car is a jack, not "put me in the next free seat". That reassign
was a warp onto a ped still climbing, and it crashed.

## Types and capabilities

`shared/vehicle/type.h` holds a `VehicleType` enum ordered to match GTA's own
`eVehicleType`, plus the capability predicates the sync layer asks:

```
VehicleHasDamageModel   VehicleHasDoors      VehicleIsAircraft
VehicleIsBike           VehicleHasLandingGear VehicleHasSirens
VehicleHasTurret        VehicleHasTaxiLight   VehicleMaxPassengers
```

**Type selects capabilities.** The table below is about what a vehicle
*has* — a bike has no `CDamageManager`, a plane has landing gear.
`RemoteVehicle` caches its type at spawn; nothing in the sync loop switches
on a model id.

Type *also* selects the physical sync policy, but through one dispatch, not
scattered `if (bike)` checks. `game/sync/vehicle/handler.cpp` picks a
`Handler` per GTA vehicle class, and that handler owns the whole physical
lifecycle for its type:

| File | Owns |
|------|------|
| `vehicle_sync.cpp` | Shared infrastructure only: packet receive, snapshot storage, interpolation timing, lookup, authority, type detection, dispatch |
| `automobile_sync.cpp` | The complete `CAutomobile` lifecycle |
| `bike_sync.cpp` | The complete `CBike` lifecycle |
| `common.cpp` | The pieces genuinely shared by more than one handler |

A car and a bike need *different* policies, not the same one minus
features — see [Bikes](#bikes) below for why. Anything used by exactly one
handler lives in that handler's file.

| Rule | Applies to |
|------|------------|
| Damage / doors / door ratios skipped | no `CDamageManager`: bikes, BMX, boats, trains |
| Rotor speed synced | helis (`m_fRotorZ` / `m_fSecondRotorZ`) |
| Landing gear written | planes only |
| Rudder controls sent | while seated (aircraft use them; MTA gates the same way) |
| No transform written | trains, and anything with `m_pTractor` set |

### An empty car's owner is re-elected by proximity

The driver keeps the car after they get out - that is what lets it roll to a
stop. But they can then walk away, drive off, or change dimension, and the car
is still theirs: simulated by a client nowhere near it, or not simulated at all.

`VehicleManager::PulseSyncers` runs each server tick and re-elects. The current
owner keeps it while within **130 m** and in the same dimension; otherwise the
nearest player within **110 m** takes over (the gap stops it churning between
two equidistant players). A vehicle with nobody in range is owned by nobody and
holds its last pose.

Occupied vehicles are skipped entirely - seat 0 owns those, and
`HandleEnter` / `HandleExit` already maintain that. MTA does the same thing in
`CUnoccupiedVehicleSync::Update`; see [MTA vehicle sync](mta-vehicle-sync.md)
section 3.

### Trains and towed vehicles get momentum, not a pose

A train's pose comes from its rail position and GTA recomputes it every frame;
a towed trailer belongs to the tow physics of whatever pulls it. Writing a
network pose over either fights the game. Both still receive move/turn speed -
that is what carries them. MTA does the same (`CNetAPI.cpp:1407`,
`CClientVehicle.cpp:2447`).

### Known open issue: `UpdateRwFrame` on the per-frame path

`SetVehiclePose` publishes the RW matrix and frame, and the interpolation
path calls it **every frame**. `CEntity::UpdateRwFrame` rebuilds the entity's
RenderWare frame hierarchy, and on a vehicle the wheels hang off that
hierarchy - so doing it 60x/s is a candidate cause of remote wheels not
visibly turning.

MTA splits the two cases and we currently do not:

| MTA | When |
|-----|------|
| `CEntitySA::SetPosition` - writes `matrix->vPos` and the placement, **no** RW update | every interpolation frame |
| `CEntitySA::SetMatrix` - full write + `UpdateRwMatrix` + `UpdateRwFrame` + `CWorld` re-bucket | warps and explicit rotation sets only |

A previous attempt to split them here also changed several other things at
once and had to be reverted wholesale, so the split has never been tested on
its own. It is the obvious next experiment for the wheel symptom, and it
should land by itself with nothing else in the same build.

See [MTA vehicle sync](mta-vehicle-sync.md) sections 9 and 37.

### Writing a pose

A pose write is three steps, not one. `CMatrix::right/up/at` is the
**physics** matrix; the renderer draws from the attached `RwMatrix` and the
frame hanging off it. So after writing the axes and the position, push both:

```
CEntity::UpdateRwMatrix   // 0x446F90
CEntity::UpdateRwFrame    // 0x532B00
```

That is the tail of MTA's `CEntitySA::SetMatrix`, and it is not optional.
A car mostly hides a missed update — `CAutomobile::ProcessControl` rebuilds
the RW matrix from its own suspension work every frame. **A bike does not.**
`CBike` caches its render transform in `m_mLeanMatrix` behind
`m_bLeanMatrixCalculated`, rebuilt from the entity matrix plus GTA's own
lean, so a matrix written and never published is read back a frame later,
half-applied — the bike renders crooked and the error accumulates.

Reading the matrix back re-orthonormalises first (Gram-Schmidt on forward
and up, the same thing `CMatrix::Reorthogonalise` does). `glm::quat_cast`
assumes a pure rotation and a GTA vehicle matrix is not reliably one; a
bike's is the worst case. Feeding shear into a quat that is then
interpolated and written back is roll drift.

We never reconstruct lean. GTA computes it; we replicate the resulting
orientation and interpolate towards it.

Health, engine, siren and packed damage ride the replica. Status is
worse-wins **per GTA field** (doors/wheels per byte, panels per nibble,
lights per 2 bits). Apply health and parts even when the car has not
moved. Do not call `SetupDamageAfterLoad` on a live car — MTA applies
each door/panel through `FixDoor` / `SetDoorDamage`. `health <= 250`
or a tank punch stays quiet so parts do not spawn under a ped.

A late joiner must see an already-blown car as a wreck. Writing
`health = 0` on a fresh `CreateCarForScript` does not change the mesh;
call `BlowUpCar` with the explosion hidden (MTA's stream-in `Blow`).
The replica latches `blown` so the server still knows after the last
driver leaves.

`BlowUpCar` clears the mission lock and stamps `m_nTimeWhenBlowedUp`.
GTA then fades the hull after 60 seconds and the streamer can drop it.
`KeepScriptVehicle` puts the lock back (`bIsLocked`, `MISSION_VEHICLE`,
`bStreamingDontDelete`) and parks the 60-second timer. Remotes do not
interpolate a wreck — that is how it falls through the map.

Disconnect is a world reset, not a warp-exit. Explosions and projectiles
first, then remotes, then cars (`Evacuate` + `ExtinguishCarFire` +
`CWorld::Remove`). `UnlinkFromVehicle` is pointer-only; dead or seated
does not change the order. Fires are put out through `ExtinguishPoint`
and `ExtinguishCarFire`, not by walking `CFire` slots.

## Steering

`m_fSteerAngle` is an **output**. GTA rebuilds it from `GetPad(0)` every
`ProcessControl`. Writing the last packet's quantized angle over that snap
the cabin every time the driver changes direction.

The actual input is a pad swap on the vehicle class's `ProcessControl`
(vtable slot 10), bound once per subclass from the live vtable
(`BindRemoteVehicleProcessControl`). Pedals come from the same pad.

Analogue stick and pedals lerp across `kPlayerMs`, same window as heading.
Buttons still snap so edges fire.

## Doors

Status bytes say what is broken. `doorRatiosPacked` (six × uint8) says how
far the hood, boot and doors are swung. A popped hood is almost always the
ratio, not a damage flag.

`CVehicle::OpenDoor` (vtable slot 27) plays the slam sample even with
`playSound = false`. Calling it every frame is the kick-and-clap loop.

Rules that keep the doors quiet:

- Call `OpenDoor` only when the current ratio is more than `2/255` from
  the target.
- Skip **every** door a ped is climbing through — local *or remote*
  (`SDK::OccupiedDoorMask`, which walks the local ped, the vehicle's
  occupants and anyone whose enter task targets it;
  `DoorRatioIndexForTarget`: 0/10 → 2, 8 → 3, 11 → 4, 9 → 5). Writing
  `OpenDoor` over a climb slams it shut. This used to consult only slot 0,
  so a *remote* player's door was written over on every packet — the door
  spam. MTA refuses the same writes in `CClientVehicle::SetDoorOpenRatio`
  (`CClientVehicle.cpp:709`), gated by `AllowDoorRatioSetting`.
- The **driver owns the swing**. A seated passenger must not report
  ratios. The server ignores ratio reports while seat 0 is taken.
- Passengers report only while **entering or leaving**, and only if the
  car has no driver — that is how an empty Sultan's door still swings on
  the other client.
- Status-only damage reports set `applyRatios = false`. A hood punch that
  sent ratios as 0 used to slam every door.
- Never max-merge ratios. Open vs shut is not "worse". Replace.

Remotes lerp the last good ratios over `kVehicleMs`, the same window
MTA uses for `SetDoorOpenRatio(..., TICK_RATE)`. A per-frame `OpenDoor`
fights a swinging or damaged door and stutters. Skip those statuses;
GTA owns the angle. If a parked door drifts, start a new lerp — do
not snap.

**An aborted entry needs its door angle on the wire.** The masking above
means nobody else may write that door, and the owner's ratio reports stop
the moment the enter task ends — but GTA swings the door shut *after* that,
as part of the ped returning to foot. So the last angle anyone heard was a
stale half-open one. `PlayerExitVehicle` carries `doorRatio` for exactly
this, the way MTA writes `GetDoorOpenRatio` into notify-abort
(`CClientPed.cpp:10443`) and forces it on receipt
(`CPacketHandler.cpp:1972`). Reporting also continues for a short settle
window after the ped lets go of the door, so the close itself is
transmitted rather than only its first frame.

## Bikes

A `CBike` is not a `CAutomobile` with two fewer wheels. Three findings from
gta-reversed shape the whole handler:

**1. Its orientation is physics, and it fights back.** Nudging the matrix
every frame is a feedback loop on two wheels. The suspension lines then sit
wrong against the ground, so GTA raises real turn forces to correct what
looks to it like a genuine attitude error, and `CPhysical` accumulates them
into `m_vecTurnSpeed` (`Physical.cpp:740/767/2548`). Measured on a bike
whose owner reported no rotation at all, `ts` grew `0.0000 → 0.0023 →
0.0124` and the bike lay down while the owner rode upright. A car absorbs
those forces through four suspension points; a bike's free axis is **roll**.

**2. Its suspension state is relative to its own position.**
`m_aGroundOffsets[i] = cp.m_vecPoint - GetPosition()` (`Bike.cpp:486`), and
`ProcessEntityCollision` compares this frame's wheel ratios against last
frame's. All of that assumes the chassis moved because *physics* moved it.
Our sync tick runs in `CTheScripts::Process` (`Game.cpp:746`), **before**
`CWorld::Process` (781) — so a per-frame position write is followed
immediately by GTA resolving wheels against ground from a chassis that was
teleported since it last measured. The chassis visibly bounces.

**3. `CBike::ProcessControlInputs` never runs for a remote bike.** It is
the only writer of `m_fSteerAngle`, the only thing that clears the
constructor's default `bIsHandbrakeOn` (`Vehicle.cpp:253`), and the only
thing that converts pad buttons into `m_fGasPedal`. So steer, handbrake and
**both pedals** are copied from the wire instead
(`SDK::ApplyBikeDriverOutputs`). The pad still reaches the engine; it does
not reach those fields.

The resulting policy:

| Authoritative | GTA's own |
|---------------|-----------|
| X, Y (where the bike is on the map) | Z, pitch, roll |
| Yaw (where the rider pointed it) | suspension, wheel contact, lean |

XY is **dead reckoned** — the packet position is constant for ~100 ms, so
easing toward it drags the bike backwards against its own motion for the
whole interval. Extrapolate by the owner's velocity for the packet's age
instead. Correct it a fraction per frame, never snap: a snap is a teleport,
and see finding 2.

Pitch is **not** an error. Two machines sit at a persistent 6–7° difference
with no corrections at all, and both are locally correct — that is each
one's own suspension under acceleration. Forcing it to zero ten times a
second *is* the chassis hop.

### Wheelies

The exception to "pitch is GTA's". During a wheelie the two machines differ
by **eighty** degrees, because the remote copy is not performing the
manoeuvre at all — the wheelie comes out of `CBike::ProcessControl`, which
never runs here.

Detect it the way GTA does. `CQuadBike::ProcessControl` (`QuadBike.cpp:222`)
reads the forward vector's Z and compares it against
`m_pHandling->m_fWheelieAng`, which `cHandlingDataMgr` stores as
`sin(angle)` (`cHandlingDataMgr.cpp:359`) — the same space, directly
comparable. Do **not** count contact wheels: `NUM_SUSP_LINES` is 4 for a
bike, so a grounded bike reports 4 contacts, not 2, and
`ProcessControlCollisionCheck` refills `m_aWheelRatios` with 1.0 before our
tick reads them. Both of those made dead gates.

While it lasts, the owner's whole orientation is authoritative, and it is
handed to the Framework `Interpolator` rather than blended per frame — the
error is captured once when the packet lands and spent across exactly one
packet window. A fixed per-frame slerp toward a target that is constant for
100 ms is not interpolation; it overshoots early and chases a stale pose
late.

## Getting knocked off a bike

Crashing a bike throws the rider. Nothing about that is an exit: no leave
task runs, and the ped never asked to get out.

**Only the owner decides.** Every remote ped is created with
`CantBeKnockedOffBike = 1` (*Never*), so a viewer's copy can never fall on
its own — each viewer would decide differently from its own collision, with
no authority to reconcile them. This is MTA's rule verbatim
(`CClientPed.cpp:5799`):

```cpp
// Set remote players to not fall off bikes locally, let them decide
if (m_bIsLocalPlayer)
    SetCanBeKnockedOffBike(m_bCanBeKnockedOffBike);
else
    SetCanBeKnockedOffBike(false);
```

The **local** ped must keep GTA's default (`0`), or nobody can be knocked
off on any machine and the whole path is dead code.

The owner reports it as `PlayerExitVehicle` with `warp = true` and
`knockedOff = true`. `warp` because they are already out — a non-warp exit
makes the server fire `vehicleStartExit`, which a script could "veto" after
the fact, and makes the receiver build a polite `CTaskComplexLeaveCar` for
someone who was launched over the handlebars.

Receivers raise GTA's own `CEventKnockOffBike` and add it to the ped's event
group. Do **not** reconstruct the response by hand — it is three steps that
have to happen together (`EventKnockOffBike.cpp`):

| Step | Address | Does |
|------|---------|------|
| `SetPedOutCar` | `0x4AFF60` | `CTaskSimpleCarSetPedOut` with `m_bKnockedOffBike`, then `RemoveCarSitAnim` |
| `CalcForcesAndAnims` | `0x4B0020` | `ApplyMoveForce` off the bike's Up/Right, returns the fall anim |
| `SetPedSafePosition` | `0x4B4AC0` | clears the bike's `LeanAngle` / `m_bLeanMatrixCalculated` |

and the responder that plays the anim
(`CEventHandler::ComputeKnockOffBikeResponse`, `0x4B9FF0`) is not reversed.
Raising the event runs all of it, including the parts nobody has read.

`m_forceKnockOff = true` is required: `AffectsPed` refuses a ped with
`CantBeKnockedOffBike` set unless it is on, and every remote ped has that
flag. `Add` returns `nullptr` when the event is refused (dead ped,
`STATUS_GHOST` vehicle, ped not in that vehicle) — fall back to a plain warp
so the rider is never left welded to the bike.

**Nothing may touch the ped while it plays.** `SDK::PedIsFallingOffBike`
gates position correction, heading interpolation, the seat warp and the
second ejection that `player_sync` would otherwise start. Getting this wrong
is what made the animation intermittent:

- The response task lands in **`TASK_PRIMARY_PHYSICAL_RESPONSE` (slot 0)**,
  not the primary slot — scan every primary slot, and follow `GetSubTask`,
  because it is a `CTaskComplexFallAndGetUp` chain whose leaf is the simple
  fall. MTA tests both ends the same way (`CClientPed.cpp:7194`).
- `CEventGroup::Add` only **queues** the event; the task does not exist
  until the ped's next `ProcessEvents`. A short grace window after raising
  it covers those frames, or the correction kills the fall before it starts.
- Do **not** use `ped->bKnockedOffBike` for this. `CalcForcesAndAnims` sets
  it and nothing in the reversed source ever clears it, so it would exclude
  the ped from position correction permanently.

The bike itself still needs an owner. The server keeps vehicle ownership
across an exit on purpose (an empty bike still rolls), but the ex-rider's
`m_pDriver` is null the instant they are ejected — so "am I driving it"
is the wrong test for whether to push its pose. An owner with **no driver
at all** keeps pushing; a passenger still refuses.

## Script spawn

`Vehicle` is a Framework `Entity`. Position is the Entity one
(`ForceState`). Rotation is MTA degrees about X, Y, Z — not a heading,
and not the Entity quaternion getter. A numeric heading on `create` is
still radians, same as `player.spawn`.

```js
const car = Vehicles.create(560, 2865.93, -1665.88, 10.88, 0);
// or Vehicle.create(560, new Vector3(x, y, z), heading)
// or Vehicles.create(560, pos, new Vector3(pitch, roll, yaw))  // degrees

car.position = new Vector3(x, y, z);
car.rotation = new Vector3(0, 0, 90); // degrees, MTA CMatrix
car.setRotation(10, 0, 180);
const { x, y, z } = car.getRotation();
car.setColor(Color.fromRGB(20, 200, 40), Color.fromRGB(8, 8, 8));
car.color3 = Color.fromRGB(255, 16, 64);
car.addUpgrade(1074); // wheels, ids 1000–1193
car.paintjob = 0;
car.engine = false;
car.locked = true;
car.overrideLights = 2; // 0 none, 1 force off, 2 force on
car.velocity = new Vector3(0, 0, 0);
car.turnVelocity = new Vector3(0, 0, 0);
car.dirtLevel = 0;
car.taxiLight = true;
car.plateText = 'VANTA';
car.frozen = false;
car.damageProof = false;
car.collisions = true;
car.landingGearDown = true; // planes; cars stay true
car.lights = true;
car.alpha = 180;
car.onGround; // owner reports bOnSolidSurface
car.engineBroken = false;
car.wheelsCanBurst = true;
car.canBeDamaged = true;
car.nitroCount = 3;
car.variant = 0;   // 255 = random extras at create
car.variant2 = 255;
car.headlightColor = { r: 0, g: 128, b: 255 }; // tints beams/coronas
car.fix();
car.blow();
car.getCompatibleUpgrades(12); // wheel ids, not filtered by model
car.setDoorOpenRatio(0, 1);
const driver = car.getController();
```

`return false` from `vehicleStartEnter` / `vehicleStartExit` vetoes the
climb or the leave. The server then sends `PlayerOccupancyAbort` so the
player who asked is stood back up (or put back in the seat). Walking
away mid-enter aborts the GTA task and tells remotes; that path does
**not** fire `vehicleStartExit`.

```js
Events.on('vehicleStartEnter', (player, vehicle, seat, jacked, door) => {
    if (vehicle.locked) {
        return false;
    }
});

Events.on('vehicleStartExit', (player, vehicle, seat, door) => {
    if (vehicle.id === lockedCarId) {
        return false;
    }
});
```

`setColor` takes one to four `Color`s, or 3 / 6 / 9 / 12 RGB bytes
(0–255). Those are real RGB, not a nearest-of-128 palette guess. GTA
only stores palette indexes on the vehicle; the client rewrites
`ms_vehicleColourTable` around `CVehicle::SetupRender` so the mesh
actually gets that green.

Create still accepts the old two palette indexes after the heading or
the rotation Vector3. They are converted to RGB on the replica.
Interior and dimension follow. See [world space](world-space.md).

A model or upgrade that is not in `catalog/gta_sa.models` is refused.
The owner never writes paint or parts back from GTA.

`CCarCtrl::CreateCarForScript` allocates from the pool, adds to the world
and gives the car physics. Mission cleanup is off so the game never deletes
a Vanta car behind us. Request the model and retry next tick if it is not
resident — a blocking load from inside the streamer hangs.

Vanta's own delete does not run `~CPhysical`. Release the realtime shadow
(`Internal::ReleaseRealTimeShadow`) before the pool-free or disconnect
crashes a frame later. Same rule for remote peds.
