# World environment

Server clock, minute length, game speed, and weather. One authority for
everyone — not a per-player replica field.

## Wire

`WorldEnv` (`shared/rpc/world_env.h`) is a reliable RPC. Flags say which
fields are live in this packet:

| Flag | Field | Meaning |
|------|-------|---------|
| time | `hour`, `minute` | CClock hours/minutes |
| minuteDuration | ms per game minute | `CClock::ms_nMillisecondsPerGameMinute` |
| gameSpeed | `0..10` | `CTimer::ms_fTimeScale` |
| weather | weather id | `CWeather::ForceWeatherNow` |

Join gets a full snapshot in `OnPlayerReady`. Script changes broadcast only
the flags that moved.

## Server clock

`WorldManager` advances time from wall ticks, not from game speed. Speeding
the sim up does not make the server day race ahead of what scripts read —
same idea as MTA's `CClock`.

Default: noon, 1000 ms per game minute, speed `1`, weather `0`.

## Anti-cheat (game speed)

A one-shot write of `ms_fTimeScale` is not enough — a trainer can poke it
again next frame. The client stores the last server speed and **rewrites
`CTimer::ms_fTimeScale` every pulse** (`PulseWorldEnv` from
`UpdatePlayerSync`). That is the whole "little anti-cheat": authority is
reasserted before physics, not detected and kicked.

## Script

```js
World.setTime(18, 30)
World.setHour(12)
World.setMinute(0)
World.setMinuteDuration(1000)   // real ms per in-game minute
World.setGameSpeed(1.0)         // clamp 0..10
World.setWeather(8)             // ForceWeatherNow id

World.getHour()
World.getMinute()
World.getMinuteDuration()
World.getGameSpeed()
World.getWeather()
```

## Backlog (MTA world features not here yet)

Water levels / triangles, sky gradient, heat haze, far clip, fog, gravity,
moon size, rain density, blur, traffic density. Those land as more
`WorldEnv` flags or sibling RPCs when needed — keep the clock path thin.
