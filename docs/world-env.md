# World environment

Server clock, minute length, game speed, weather, gravity, and optional
visual overrides (far clip, fog, rain, waves, sky, moon, sun, water colour,
heat haze, clouds, jetpack height, wind). One authority for everyone — not a
per-player replica field.

## Wire

`WorldEnv` (`shared/rpc/world_env.h`) is a reliable RPC. Flags are `uint32`
and say which fields are live in this packet:

| Flag | Field | Meaning |
|------|-------|---------|
| time | `hour`, `minute` | CClock hours/minutes |
| minuteDuration | ms per game minute | `CClock::ms_nMillisecondsPerGameMinute` |
| gameSpeed | `0..10` | `CTimer::ms_fTimeScale` |
| weather | weather id | `CWeather::ForceWeatherNow` |
| gravity | `0..1` (default `0.008`) | global gravity float |
| farClip | distance | `CRenderer` far clip plane |
| fog | distance | fog start |
| rain | `0..1` | `CWeather::Rain` |
| wave | height ≥ 0 | water wave height |
| sky | 6 RGB bytes | sky top / bottom gradient |
| moon | size (default `3`) | moon sprite size |
| sunColor | 6 RGB bytes | sun core + corona |
| sunSize | unscaled float | stored as `value * 10` in GTA |
| waterColor | RGBA floats `0..255` | surface + underwater tint |
| heatHaze | intensity + FX params | `CPostEffects` heat haze |
| clouds | bool | volumetric / bottom / skyline / trails |
| jetpackHeight | float (default `100`) | jetpack ceiling |
| wind | xyz floats | weather wind velocity |

Join gets core flags (time / duration / speed / weather / gravity) plus any
visual overrides script has already set. Script changes broadcast only the
flags that moved.

## Server clock

`WorldManager` advances time from wall ticks, not from game speed. Speeding
the sim up does not make the server day race ahead of what scripts read —
same idea as MTA's `CClock`.

Default: noon, 1000 ms per game minute, speed `1`, weather `0`, gravity
`0.008`, clouds on. Visual overrides stay off until script sets them.

## Anti-cheat (speed and gravity)

A one-shot write of `ms_fTimeScale` or gravity is not enough — a trainer can
poke it again next frame. The client stores the last server values and
**rewrites them every pulse** (`PulseWorldEnv` from `UpdatePlayerSync`).
Authoritative visuals are pulsed the same way so weather / timecycle code
cannot stomp them without MTA-style Update nops.

## Script

```js
World.setCloudsEnabled(false)
World.setJetpackMaxHeight(100)
World.setWindVelocity(0.2, 0.1, 0.0)

World.getCloudsEnabled()
World.getJetpackMaxHeight()
World.getWindVelocity()   // [x, y, z]
// …plus earlier World.* getters/setters
```

## Backlog (MTA world features not here yet)

- **Water levels / `createWater` triangles** — needs a client water vertex
  manager (MTA's `CWaterManagerSA`), not a single float.
- Aircraft max height / velocity (multiplayer pointer redirects).
- Blur level — Vanta already NOPs speed blur in `gameplay.cpp`.
- Traffic density — not a first-class MTA world sync.
