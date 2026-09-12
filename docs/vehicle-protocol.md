# Vehicle wire protocol

How vehicle state travels. This is the **network** layer; how a `CBike` and a
`CAutomobile` differently *consume* that state is [`vehicles.md`](vehicles.md)
and `game/sync/vehicle/`. The two are deliberately separate — the same
`VehicleDriverSync` is applied by `BikeSync` and `AutomobileSync` in different
ways, and that is an apply policy, not a wire format.

## The layering

```
semantic network packets
        |
        v
common decoded network state
        |
        v
vehicle type dispatcher        (game/sync/vehicle/handler.cpp)
        |
        +--> BikeSync
        +--> AutomobileSync
        +--> future handlers
```

Packets are split by **semantic purpose, authority, reliability and cadence** —
never by GTA vehicle class. There is no `BikeSyncPacket`.

## What was already here

This refactor did **not** start from a flat snapshot, and the numbers below
only make sense against what MafiaNet already does.

`NetworkEntity::Serialize` splits every replicated entity across two channels:

| | Channel 0 `Transform` | Channel 1 `State` |
|---|---|---|
| Reliability | `Unreliable` | `ReliableOrdered` |
| Contents | position, velocity, rotation — raw | game fields, VDS delta |
| Idle | memcmp-deduped by RM3 | `Reset()`, no message sent |

The state channel runs through `VariableDeltaSerializer`. `WriteVarToBitstream`
writes **one change-bit per field** and the value only when it changed, so
"omitted means do not modify" was already true there, and a `bool` already cost
one bit. A parked vehicle already cost ~0 B/s.

So the wins below come from **cadence, batching and quantization**, not from
introducing delta encoding — that already existed.

## The packet set

| Packet | Identifier | Reliability | Cadence | Authority |
|---|---|---|---|---|
| `VehicleDriverSync` | `VMP:VD` | **Unreliable** | 100 ms while driven | driver (seat 0) only |
| `VehicleUnoccupiedSync` | `VMP:VU` | **Unreliable** | 400 ms, only if changed | assigned syncer only |
| `VehicleResync` | `VantaMP::VehicleResync` | **Reliable** | on divergence / handoff / request | server |
| `VehicleDamageReport` | `VantaMP::VehicleDamageReport` | **ReliableOrdered** | on event | any witness, validated |
| `VehicleSyncOwner` | `VantaMP::VehicleSyncOwner` | **ReliableOrdered** | on handoff | server |
| `PlayerEnterVehicle` | existing | **ReliableOrdered** | on event | client, server stamps seat |
| `PlayerExitVehicle` | existing | **ReliableOrdered** | on event | client |
| `PlayerOccupancyAbort` | existing | **ReliableOrdered** | on event | server |

### Why unreliable for motion

There is no value in retransmitting snapshot N once N+1 has arrived — a late
one drags the car backwards. Motion is `Unreliable` and every receiver drops
anything older than the newest pose applied, using a `uint16 sequence` compared
by signed difference so wrap is harmless:

```cpp
IsNewerSequence(incoming, applied)   // (int16_t)(incoming - applied) > 0
```

A plain `>` would stall a car for ~109 minutes at the wrap point and would
accept reordered datagrams. Loss self-heals: the next packet is 100 ms behind.

### Why reliable for the rest

Enter/exit/abort/spawn/handoff/damage are **events**. They cannot be expressed
as "the next motion packet will imply it": a flag that plays once restarts every
tick, and an unreliable event that is missed leaves a player standing beside a
car everyone else sees them driving. Ordering matters for `VehicleSyncOwner` in
particular — a stop and start that swap leave two clients simulating one car, or
none.

## Change masks: presence is not value

Every masked packet branches on the **mask bit**, never on the decoded value:

```cpp
if (fields & BRAKE)      state.brake = packet.brake;   // correct
if (packet.brake)        state.brake = packet.brake;   // WRONG
```

Zero is a valid value for every one of these fields — a released throttle, a car
at rest, a repaired panel. **Omitted means do not modify. Transmitted zero means
set to zero.**

This matters most in `VehicleDamageReport`, where the server merges worse-wins:
merging an absent group read as zero would silently *repair* damage the reporter
simply had nothing to say about. The old struct's `applyRatios` bool existed to
work around exactly this for one field; the mask generalises it and that flag is
gone.

## Quantization — and the error measured for each

Every choice was measured, not assumed. Nothing here was chosen because it
"looked smaller".

| Field | Encoding | Bits | Measured error |
|---|---|---|---|
| Position | fixed point, ±8192 m | 26/axis | max **0.12 mm** across the playable map |
| Rotation | smallest-three quaternion | 2 + 3×16 = 50 | max **0.0035°** over 200k random orientations |
| Velocity | MTA `SVelocitySync` | **1** when zero, else 81 | direction 16 bits/axis |
| Turn speed | same as velocity | 1 / 81 | as above |
| Health | fixed point 0–2047.5 | 12 | 0.5 step (MTA's exact scheme) |
| Controls | native `int8`/`uint8` | 40 | **lossless** |
| Flags | packed bits | 6 | lossless |
| Vehicle id | compact wire id | 16 | — |

Rotation deserves the note: **0.0035° is far below what a bike's suspension
moves in a single frame**, which is the error that actually matters given
`BikeSync` deliberately preserves GTA-owned pitch/roll. The network source state
is full 3D orientation regardless — the apply policy decides what to consume, and
compression must never change the meaning of orientation.

Controls were *not* quantized further: GTA's `m_fSteerAngle` and pedals are
already read into `int8`/`uint8` at the SDK edge, so there was nothing to win,
and a narrower steer would show as a stepped wheel.

### The compact wire id

`WireId` is 12 bits of slot + 4 bits of generation. The canonical identifier is
**unchanged** — `NetworkID` stays 64-bit and every cold packet keeps using it.
This is a hot-path addressing form only, worth 6 bytes on every motion packet
and every batch entry.

The generation half is **not optional**: a slot freed and reissued comes back
with a different generation, so a motion packet still in flight for the old
occupant fails to resolve instead of silently steering its replacement. 4096
slots is far above GTA's ~110-vehicle pool.

## Measured packet sizes

Payload bytes, from `proto_bench` against the real `MafiaNet::BitStream`:

| Packet | min | typical | max |
|---|---|---|---|
| `VehicleDriverSync` | 15 | **47** | 49 |
| `VehicleUnoccupiedSync` (1 entry) | — | 29 | 40 |
| `VehicleUnoccupiedSync` (batch of 20) | — | **377** (18.9/vehicle) | — |
| `VehicleResync` | — | 49 | 49 |
| `VehicleDamageReport` | 13 (one group) | — | 40 (all seven) |
| `VehicleSyncOwner` | 11 | 11 | 11 |

Add **~9 bytes of RPC4 header** to each. That header is why hot identifiers are
short: `RPC4::Signal` Huffman-encodes the identifier *string* into every packet,
so `"VantaMP::VehicleDriverSync"` would cost 23 B — more than half the payload —
where `"VMP:VD"` costs 9. The replica path pays none of this, which is why
moving state off the replica is not automatically cheaper.

## Bandwidth: before vs after

| Scenario | Before | After | Delta |
|---|---|---|---|
| 1 driven vehicle | 706 B/s | 560 B/s | **−21%** |
| 20 rolling unoccupied | 14120 B/s | 965 B/s | **−14.6×** |
| 20 parked | ~0 B/s | ~0 B/s | no change |

Read these honestly:

- The driven-vehicle win is **modest**, because the RPC4 header gives back a
  third of what quantization saves. The encoding is much tighter; the transport
  is not free.
- The unoccupied win is **large**, and most of it is the 400 ms cadence and
  batching — not the encoding.
- Parked vehicles were **already** free. RM3's memcmp dedupe and the suppressed
  state channel did that before this refactor, and nothing here improved it.

## Behaviour under packet loss

Measured with the real serializers driving a simulated 200-second drive, with
loss and reordering applied to the stream (`loss_sim`):

| Loss | Bytes/packet | Worst position error | Mean | Longest field divergence |
|---|---|---|---|---|
| 0% | 29.3 | 2.80 m | 0.04 m | 0.9 s |
| 5% | 29.3 | 2.80 m | 0.11 m | 0.5 s |
| 20% | 29.3 | 5.60 m | 0.40 m | 2.2 s |
| 50% | 29.3 | 12.56 m | 1.39 m | 3.6 s |

Two things worth being precise about:

- **Position recovers on the next packet that carries it**, so visible error
  stays small even at heavy loss — and what error there is gets absorbed by the
  receiver's interpolation rather than shown as a jump.
- **The keyframe interval bounds how often full state is sent, not how long
  divergence lasts.** A keyframe is as droppable as anything else on an
  unreliable channel, so recovery time is a *multiple* of the interval: 1 s
  nominal becomes 3.6 s at 50% loss. That is still bounded and self-healing,
  which is the requirement — but "bounded by 1 s" would have been wrong.

The 29.3 B/packet measured here is **below** the 47 B typical quoted above,
because in steady driving most ticks do not change every field. 47 B is the
figure for a tick that changes all of them; 29 B is what a real drive averages.

## MTU

`MAXIMUM_MTU_SIZE` is 1400. Batches stop at `kMaxPayloadBytes = 1100`, leaving
room for RakNet's datagram and reliability headers plus the RPC4 identifier.
Worst-case entry is 40.4 B, so ~27 entries fit; the sender closes a batch and
starts another rather than letting one datagram fragment, because losing one IP
fragment loses the whole datagram.

## Update rates

| Class | Rate | Why |
|---|---|---|
| Driven, in range | 100 ms | latency-sensitive; what everyone is watching |
| Unoccupied, moving | 400 ms | nobody is steering it; MTA's number |
| Unoccupied, stationary | **never** | no periodic motion sync at all unless state changed |
| Out of interest range | **never** | the interest grid culls it |

Interest management is the framework's existing `InterestGrid` — distance,
dimension, hysteresis, per-type budgets. No second streamer was added.

## Authority

| Packet | Accepted from |
|---|---|
| `VehicleDriverSync` | the vehicle's owner **and** seat 0. A passenger is rejected. |
| `VehicleUnoccupiedSync` | the currently assigned syncer only |
| `VehicleDamageReport` | a player in range, validated, merged worse-wins |
| `VehicleResync` | server only |

Every inbound packet is validated before it is applied or rebroadcast: finite
values, world bounds, velocity bounds, sequence recency, and sender ownership.

## Status

**Landed and building (client + server):**

- the packet set and quantizers (`shared/rpc/vehicle/*`), measured by
  `proto_bench` — 50+ roundtrip, mask-semantics, authority, NaN, wire-id reuse
  and MTU checks pass
- **driver sync end to end**: client sends masked deltas + 1 s keyframe; server
  validates authority (incl. the passenger rule), NaN/bounds and sequence, keeps
  the replica current and relays Unreliable minus the sender; client applies by
  mask into `VehicleEntity`, so `BikeSync`/`AutomobileSync` are untouched
- **replica transform suppression** while driver sync is live — a presence bit
  on the wire, not a local flag test, so writer and reader can never disagree
  about the message shape
- **unoccupied sync end to end**: server elects the nearest in-range player as
  syncer once a second and hands off stop-before-start; that client batches
  deltas at 400 ms respecting the MTU; server validates *per entry* and relays
- **compact wire ids**, minted only by the server, announced on create and to
  each joiner, released on destroy (generation bumped)
- `shared/vehicle/validate.h` **wired up**. It existed before this work and was
  never included anywhere: no NaN, Inf or world-bounds check ran on any
  incoming vehicle state, on the server or the client
- `VehicleDamageReport` authority — the old handler checked only that the sender
  was a connected player, so any client could wreck any vehicle anywhere on the
  map and have the server merge and rebroadcast it as authoritative
- **VERSION 0.1.0 -> 1.0.0** (see above)

**Not done:**

1. `VehicleResync` is defined, validated and tested, but nothing *sends* it yet.
   The divergence detector that should trigger it does not exist.
2. The driver-ped transform redundancy (below) — the largest saving left.
3. Trailers (`VehicleTrailerAttach/Detach`) were scoped but not built; nothing
   in VantaMP tows anything yet, so there is no fact to synchronise.

**Not yet tested in a running game.** Everything above builds and the protocol
logic is tested in isolation against the real `BitStream`, but the loss figures
come from a simulation, not from the network. Bike behaviour in particular
wants a two-client run before it is trusted.

### The driver-ped redundancy

`PushLocalState` writes `entity->position/velocity/rotation` every tick,
*including while driving*. A seated ped's transform **is** the vehicle's — the
receiver already derives it from occupancy — so that is ~41 B/tick per driver of
pure duplication. What still has to be sent while driving is weapon, aim/driveby
and animation flags; not the transform.

This is the `ONE FACT = ONE NETWORK SOURCE OF TRUTH` rule, and it is still
violated on the player path.

## Files

```
shared/rpc/vehicle/
    common.h            quantizers, change-mask constants, flag bits
    wire_id.h           compact slot+generation id
    driver_sync.h       VehicleDriverSync
    unoccupied_sync.h   VehicleUnoccupiedEntry + batch
    resync.h            VehicleResync
    damage.h            VehicleDamageReport
    sync_owner.h        VehicleSyncOwner (start/stop)
    metrics.h           per-packet-type counters

shared/rpc/
    player_enter_vehicle.h      unchanged — already correct semantic events
    player_exit_vehicle.h
    player_occupancy_abort.h
```

Occupancy packets were left where they are. They were already semantic,
reliable and correctly shaped; moving them would have been churn.
