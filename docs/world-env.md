# World environment

Server clock, minute length, game speed, weather, gravity, and optional
visual overrides (far clip, fog, rain, waves, sky, moon, sun). One authority
for everyone — not a per-player replica field.

## Wire

`WorldEnv` (`shared/rpc/world_env.h`) is a reliable RPC. Flags say which
fields are live in this packet:

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

Join gets core flags (time / duration / speed / weather / gravity) plus any
visual overrides script has already set. Script changes broadcast only the
flags that moved.

## Server clock

`WorldManager` advances time from wall ticks, not from game speed. Speeding
the sim up does not make the server day race ahead of what scripts read —
same idea as MTA's `CClock`.

Default: noon, 1000 ms per game minute, speed `1`, weather `0`, gravity
`0.008`. Visual overrides stay off until script sets them.

## Anti-cheat (speed and gravity)

A one-shot write of `ms_fTimeScale` or gravity is not enough — a trainer can
poke it again next frame. The client stores the last server values and
**rewrites them every pulse** (`PulseWorldEnv` from `UpdatePlayerSync`).
Authoritative visuals are pulsed the same way so weather / timecycle code
cannot stomp them without MTA-style Update nops.

## Script

```js
World.setTime(18, 30)
World.setHour(12)
World.setMinute(0)
World.setMinuteDuration(1000)   // real ms per in-game minute
World.setGameSpeed(1.0)         // clamp 0..10
World.setWeather(8)             // ForceWeatherNow id
World.setGravity(0.008)
World.setFarClip(800)
World.setFogDistance(200)
World.setRainLevel(0.5)
World.setWaveHeight(0.6)
World.setSkyGradient(30, 60, 120, 10, 20, 40)
World.setMoonSize(3)
World.setSunColor(255, 200, 100, 255, 255, 200)
World.setSunSize(1.0)

World.getHour()
World.getMinute()
World.getMinuteDuration()
World.getGameSpeed()
World.getWeather()
World.getGravity()
World.getFarClip()
World.getFogDistance()
World.getRainLevel()
World.getWaveHeight()
World.getSkyGradient()   // [topR, topG, topB, botR, botG, botB]
World.getMoonSize()
World.getSunColor()      // [coreR, coreG, coreB, coronaR, coronaG, coronaB]
World.getSunSize()
```

## Backlog (MTA world features not here yet)

Water level vertex rewrites / `createWater` triangles, heat haze, blur
(often per-player in MTA), traffic density, water colour.
Those land as more `WorldEnv` flags or sibling RPCs when needed.
