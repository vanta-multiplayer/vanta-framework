# World environment

Server clock, minute length, game speed, weather, gravity, and optional
visual overrides (far clip, fog, rain, waves). One authority for everyone —
not a per-player replica field.

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

Join gets core flags (time / duration / speed / weather / gravity) plus any
visual overrides script has already set. Script changes broadcast only the
flags that moved.

## Server clock

`WorldManager` advances time from wall ticks, not from game speed. Speeding
the sim up does not make the server day race ahead of what scripts read —
same idea as MTA's `CClock`.

Default: noon, 1000 ms per game minute, speed `1`, weather `0`, gravity
`0.008`. Far clip / fog / rain / waves stay off until script sets them.

## Anti-cheat (speed and gravity)

A one-shot write of `ms_fTimeScale` or gravity is not enough — a trainer can
poke it again next frame. The client stores the last server values and
**rewrites them every pulse** (`PulseWorldEnv` from `UpdatePlayerSync`).
Authoritative far clip / fog / rain / wave are pulsed the same way so weather
code cannot stomp them without MTA-style Update nops.

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
```

## Backlog (MTA world features not here yet)

Water level vertex rewrites / `createWater` triangles, sky gradient, heat
haze, moon/sun size, blur (often per-player in MTA), traffic density.
Those land as more `WorldEnv` flags or sibling RPCs when needed.
