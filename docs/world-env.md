# World environment

Server-authoritative clock and world switches. One authority for everyone —
not a per-player replica field. Distinct from [world space](world-space.md)
(interiors / dimensions on entities).

## Files

| Side | Path |
|------|------|
| Wire | `code/shared/rpc/world_env.h` |
| Server state | `code/server/src/game/world_manager.*` |
| Script | `code/server/src/scripting/world.*` (`World` global) |
| Client apply | `code/client/src/game/sync/world_sync.*` |
| Client GTA | `code/client/src/sdk/world/environment.*` (not on the plugin-sdk wall) |
| Patterns | `code/client/src/sdk/patterns.*` |

Join snapshot: `Server::OnPlayerReady` → `WorldManager::SendTo`. Script
sets broadcast only the flags that moved. Client registers
`WorldEnv` in `core/net.cpp` and pulses authority from `UpdatePlayerSync`.

**Restart the server (and clients) after any `WorldEnv` serialize change.**
The RPC is positional like the entity serializers.

## Wire

`WorldEnv` is a reliable RPC. Flags are `uint32` and say which fields are
live in this packet:

| Flag | Field | Meaning |
|------|-------|---------|
| time | `hour`, `minute` | CClock hours/minutes |
| minuteDuration | ms per game minute | `CClock::ms_nMillisecondsPerGameMinute` |
| gameSpeed | `0..10` | `CTimer::ms_fTimeScale` |
| weather | weather id | `CWeather::ForceWeatherNow` |
| gravity | `0..1` (default `0.008`) | global gravity float |
| farClip | distance | far clip plane |
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

**Core on join** (always): time, minuteDuration, gameSpeed, weather, gravity.

**Optional** (OR'd into the join snapshot only after script has set them):
farClip, fog, rain, wave, sky, moon, sunColor, sunSize, waterColor,
heatHaze, clouds, jetpackHeight, wind.

## Server clock

`WorldManager` advances time from wall ticks, not from game speed. Speeding
the sim up does not make the server day race ahead of what scripts read —
same idea as MTA's `CClock`.

Defaults: noon, 1000 ms per game minute, speed `1`, weather `0`, gravity
`0.008`, clouds on. Other visuals stay off until script sets them.

## Anti-cheat (speed, gravity, visuals)

A one-shot write of `ms_fTimeScale` or gravity is not enough — a trainer can
poke it again next frame. The client stores the last server values and
**rewrites them every pulse** (`PulseWorldEnv` from `UpdatePlayerSync`).
Authoritative visuals are pulsed the same way so weather / timecycle code
cannot stomp them without MTA-style Update nops.

## Script API

```js
// Clock
World.setTime(18, 30)
World.setHour(12)
World.setMinute(0)
World.setMinuteDuration(1000)   // real ms per in-game minute
World.getHour()
World.getMinute()
World.getMinuteDuration()

// Speed / weather / gravity
World.setGameSpeed(1.0)         // clamp 0..10
World.setWeather(8)             // ForceWeatherNow id
World.setGravity(0.008)
World.getGameSpeed()
World.getWeather()
World.getGravity()

// Clip / weather FX
World.setFarClip(800)
World.setFogDistance(200)
World.setRainLevel(0.5)
World.setWaveHeight(0.6)
World.getFarClip()
World.getFogDistance()
World.getRainLevel()
World.getWaveHeight()

// Sky / celestial
World.setSkyGradient(30, 60, 120, 10, 20, 40)
World.setMoonSize(3)
World.setSunColor(255, 200, 100, 255, 255, 200)
World.setSunSize(1.0)
World.getSkyGradient()          // [topR, topG, topB, botR, botG, botB]
World.getMoonSize()
World.getSunColor()             // [coreR, coreG, coreB, coronaR, coronaG, coronaB]
World.getSunSize()

// Water / post FX
World.setWaterColor(64, 128, 200, 200)   // RGB[A], 0..255
World.setHeatHaze(80)                   // intensity; rest optional
World.setHeatHaze(80, 0, 12, 18, 75, 80, 80, 85, false)
World.getWaterColor()                   // [r, g, b, a]
World.getHeatHaze()                     // [intensity, randomShift, …, inside]

// Clouds / jetpack / wind
World.setCloudsEnabled(false)
World.setJetpackMaxHeight(100)
World.setWindVelocity(0.2, 0.1, 0.0)
World.getCloudsEnabled()
World.getJetpackMaxHeight()
World.getWindVelocity()                 // [x, y, z]
```

## Backlog

- **Water levels / `createWater` triangles** — needs a client water vertex
  manager (MTA's `CWaterManagerSA`), not another `WorldEnv` float.
- Aircraft max height / velocity (multiplayer pointer redirects).
- Blur level — Vanta already NOPs speed blur in `gameplay.cpp`.
- Traffic density — not a first-class MTA world sync.
