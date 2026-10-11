# Wire quantization

Every encoding choice in `shared/rpc/common/quantize.h`, and the measured error
that justifies it. Shared by the player and vehicle protocols, because a metre
is a metre whichever kind of entity is standing on it.

Nothing here was chosen because it looked smaller. Each bound is sized to what
GTA:SA can actually produce, and each error figure below is **measured over
300k random samples against the real encoder**, not derived on paper.

## The numbers

| Field | Encoding | Bits | Measured worst error |
|---|---|---|---|
| Position X/Y | fixed point, ±4096 m | 23 each | **0.73 mm** |
| Position Z | fixed point, −256..1792 m | 20 | **1.04 mm** |
| Velocity | 1 bit if zero, else 14 bits/axis over ±128 m/s | 1 or 43 | **7.8 mm/s** |
| Angles (heading, camera, aim) | ±π | 16 | 0.0055° |
| Vehicle rotation | smallest-three quaternion | 50 | 0.0035° |
| Health | 0..2047.5 | 12 | 0.5 |
| Sequence | wrap-compared | 8 | — |
| Entity id | slot + generation | 16 | — |
| Sticks | ±128 scaled 127/128 | 8 each | 1 unit |

## Why these bounds

**Position — 66 bits, was 78.** GTA:SA's playable map is 6000×6000, so the old
±8192 bound spent a bit per axis on space the game cannot reach. ±4096 is still
wider than the map, so a script teleport past the edge clamps somewhere sane.

Z is not X: height runs about −100 (sea floor) to 1500 (Mt Chiliad plus an
aircraft ceiling), so giving it the same 23 bits as a horizontal axis wasted
three of them on altitudes nothing can occupy. 20 bits over −256..1792 still
gives 1.04 mm, which matters because **a bike's suspension travel is vertical**
and a coarse Z would show as chassis bounce.

MTA ships 10 fractional bits (~1 mm) over a comparable range and nobody reports
jitter, so this is the same order of precision a shipping game found sufficient.

**Velocity — 43 bits, was 81.** This is the single biggest win, and it was pure
waste. MTA's `SVelocitySync` writes a zero-bit, then a **float32 magnitude** and
a 48-bit normalized direction. A float32 spans 1e38 to carry a number that never
leaves ±90: GTA's fastest car does ~65 m/s, terminal velocity is ~50, a Hydra in
a dive ~90.

Quantizing each axis directly costs 14 bits. The 7.8 mm/s error, integrated over
a full 100 ms tick, drifts **0.78 mm** — below the position quantizer's own step,
so it cannot be the visible error.

The zero case still costs **one bit**, which is what keeps an unoccupied,
stationary vehicle free.

`VehicleDriverSync` carries two velocity-shaped fields (velocity and turn speed),
so it saves 76 bits.

**Sequence — 8 bits, was 16.** The sequence only has to reject a datagram that
arrived after a newer one; it never has to be globally unique. At 10 Hz a uint8
wraps every 25.6 s and the comparison treats anything beyond 127 ticks (12.8 s)
as older. A datagram twelve seconds late is not a reorder — it is a packet the
transport gave up on, and applying it would be wrong anyway. The uint16 bought a
109-minute window nothing could use.

## Safety

`SerializeFixed` **clamps rather than wraps**: an out-of-range value lands at the
edge of the world, never on the far side of it. NaN is pinned explicitly — it
fails the ordinary range comparisons and would otherwise convert to an arbitrary
integer. Verified: a NaN position comes back as a finite clamped value.

Angles are the exception: they **wrap** rather than clamp, because an angle just
past π must return just past −π. A clamp there would make an entity turning
through north stick and then jump.

A non-finite velocity is written as zero rather than quantized, since quantizing
it would pin an axis to ±128 m/s and shove the entity.

## Packet sizes after

| Packet | Bits | Bytes |
|---|---|---|
| `VehicleDriverSync` typical | 279 | 35 |
| `VehicleDriverSync` min | 97 | 13 |
| `VehicleUnoccupiedSync` entry | 180 | 23 |
| `PlayerOnFootSync` walking | 222 | 28 |
| `PlayerOnFootSync` aiming | 254 | 32 |
| `PlayerOnFootSync` input only | 94 | 12 |

MTU headroom improved with the entries: the unoccupied batch now fits 37
worst-case entries per datagram instead of 23, and 200 entries take 6 datagrams
instead of 8.
