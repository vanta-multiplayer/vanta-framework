# Core <-> UI events

The page talks through `ui/src/lib/vanta.ts`. Arguments are positional.
C++ owns the endpoints on `http://vanta/`. There is no invoke, no
VantaNui, no JS the host evaluates.

```
UI  vanta.emit(name, ...args)   →  POST /emit
Core AddEventListener(name)     ←

Core Emit(name, [args...])      →  queue
UI  GET /events                 ←
```

C++ names live in `code/client/src/core/nui/events.h`. Core listens with
`Nui::On` and talks to the page with `Nui::Emit`.

Resource views still use `Web.emit` / `Web.on` on their own CEF views.

## Wired now

### Core -> UI

| Event | Arguments | When |
|-------|-----------|------|
| `version:update` | `version, branch, lastIp, debug, earlyLoad` | After `loaded` |
| `version:ready` | | After version/settings/servers |
| `settings:update` | `data` | After `loaded`; later on change |
| `servers:update` | `servers` | After `loaded`; empty until a masterlist exists |
| `servers:recent:update` | `servers` | After `loaded` |
| `servers:favorite:update` | `servers` | After `loaded` |
| `ui:ready` | | Game startup finished (frontend bypassed) |
| `ui:toggle` | `state?` | Core wants the F1 menu open or shut |
| `connection:setServer` | `serverName` | Connect started |
| `connection:connecting` | `serverName?` | TCP connect |
| `connection:connected` | `serverId?` | Accepted |
| `connection:failed` | `message, allowReconnect?` | Could not reach |
| `connection:disconnected` | `message` | Dropped |
| `console:push` | `color, data` | A log line |
| `console:open` | `state` | Echo of `console:setState` |
| `netgraph:update` | `sample` | Extra HUD sample |

### UI -> Core

| Event | Arguments | What Core does |
|-------|-----------|----------------|
| `loaded` | | Pushes version, settings, servers, then `version:ready` |
| `connection:connect` | `host, port, cdn, auth, password, id, name, applyTheme` | Join |
| `connection:abort` | | Logged; disconnect not wired yet |
| `console:execute` | `command` | Log + echo via `console:push` |
| `console:setState` | `state` | Echo `console:open` |
| `servers:reload` | | `servers:update` with `[]` |
| `settings:change` | `key, value` | Save display keys |
| `ui:toggle` | `state?` | Cursor / menu focus |
| `netgraph:enable` | `visible` | Reserved |
| `exit` | | `PostQuitMessage` |

## Reserved (not implemented)

Connection stages: `connection:idle`, `joining`, `startingResources`,
`reloadingGameMap`, `preloadingMods`, `startingGame`,
`downloadingResources`, `validatingResources`, `downloadingRuntimes`,
`validatingRuntimes`, `inQueue`, `downloadingAdditionalResources`,
`validatingAdditionalResources`, `requestPermissions`,
`connection:accept`, `connection:deny`, `connection:reconnect`.

Console: `console:end`, `console:reset`, `console:clear`,
`console:forceTransparent`, `console:openLogFile`.

Servers: `servers:updateError`, `servers:setPing`, `serverData:*`,
`servers:favorite:add/remove`.

Misc: `settings:devices:*`, `settings:currentVolume:*`, `version:setRss`,
`version:setManifest`, `ui:startupProgress`, `ui:setWatermarkPosition`,
`ui:setEarlyAuthState`, `ui:customLocales`, `ui:resetSkin`, `keydown`.

## Example flow

```
UI  emit loaded
Core emit version:update  "0.1.0", "develop", "", false, false
Core emit settings:update { nickname, lastServer, ... }
Core emit servers:update  []
Core emit version:ready
Core emit ui:ready                    (game finished starting)

UI  emit connection:connect  "127.0.0.1", 22005, "", "", "", "", "Player", false
Core emit connection:setServer        "127.0.0.1"
Core emit connection:connecting       "127.0.0.1:22005"
Core emit connection:connected        "127.0.0.1"
Core emit ui:toggle                   false
```
