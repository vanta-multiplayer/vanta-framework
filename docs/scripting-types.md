# Scripting types

Ambient TypeScript for resource scripts lives in [`types/`](../types/README.md)
as **`@vantamp/types`**.

| Entry | Use for |
|-------|---------|
| `@vantamp/types/server` | `serverScripts` |
| `@vantamp/types/client` | `clientScripts` |

## Runtime globals

**Server:** Framework value types + `Events` / `Chat` / …, plus Vanta
`Player` (extends Framework `Player` → `Entity`), `Vehicle` / `Vehicles`,
`World`.

**Client:** same Framework client surface (`Key`, `Discord`, `Web`, …), plus
Vanta **readonly** `Player` / `Vehicle` handles over streamed replicas
(`localPlayer` / `Player.getLocal()`, `player.getOccupiedVehicle()`, vehicle
occupants). No `Vehicles.create`, `World`, spawn, or kick — those stay
server-side.

## Regenerate

```bat
scripts\dump_scripting_catalog.ps1 -Side server
scripts\build.bat types
```

Client dump needs a game session (`-Side client`); until then the generator
bootstraps `catalog/client.json` from the server dump (stripping server-only
methods) and keeps Key / Discord from Framework vendor files.
