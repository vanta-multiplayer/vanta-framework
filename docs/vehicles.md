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

## Motion

Only the driver writes pose. Remotes interpolate.

- Delay range 100–400 ms. Position compensation can go to 1.5.
- Error = target − current, scaled by `lerp(0.25, 1, unlerp(100, delay, 400))`.
- Extrapolate about 5 ms of GTA velocity so the car does not sit still
  between packets.
- Warp at `15 + 10 × speed`, then `× tick/100`. Tighter numbers snap a
  moving car every packet, and a car that warps while someone is punching
  it launches them.
- Do not interpolate the local driver's (or syncer's) car.
- After the pose correction, write move speed back. `SetPosn` and collision
  from the correction can kill the speed the game just integrated.

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
- Skip the door the local ped is climbing through
  (`DoorIndexFromTarget`: 0/10 → 2, 8 → 3, 11 → 4, 9 → 5). Writing
  `OpenDoor` over a climb slams it shut.
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

## Script spawn

```js
Vehicles.create(560, 2865.93, -1665.88, 10.88, 0);
```

`CCarCtrl::CreateCarForScript` allocates from the pool, adds to the world
and gives the car physics. Mission cleanup is off so the game never deletes
a Vanta car behind us. Request the model and retry next tick if it is not
resident — a blocking load from inside the streamer hangs.

Vanta's own delete does not run `~CPhysical`. Release the realtime shadow
(`Internal::ReleaseRealTimeShadow`) before the pool-free or disconnect
crashes a frame later. Same rule for remote peds.
