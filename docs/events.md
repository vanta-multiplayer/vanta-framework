# Core <-> UI events

The page talks through `ui/src/lib/vanta.ts`. Arguments are positional.
C++ owns the endpoints on `http://vanta/`. There is no invoke, no
VantaNui, no JS the host evaluates.

```
UI  vanta.emit(name, ...args)   →  POST /emit
Core On(name)                   ←

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
| `version:update` | `version, branch, environment, lastIp, debug, earlyLoad` | After `loaded`. `environment` is `test` or `release` |
| `version:ready` | | After version/settings/servers |
| `settings:update` | `data` | After `loaded`; later on change |
| `settings:devices:update` | `{ input, output }` | After `loaded`; on reload |
| `settings:currentVolume:update` | `level` | Mic test or local talk meter |
| `settings:currentVolume:running` | `running` | Mic test started or stopped |
| `servers:update` | `servers` | After `loaded`; empty until a masterlist exists |
| `servers:recent:update` | `servers` | After `loaded` |
| `servers:favorite:update` | `servers` | After `loaded` |
| `ui:ready` | | Game startup finished (frontend bypassed) |
| `ui:toggle` | `state?` | Core wants the pause menu open or shut |
| `hotkey:escape` | | Esc while the menu is already open |
| `connection:setServer` | `serverName` | Connect started |
| `connection:invite` | `server` | Launched with `vanta://` or `--connect` |
| `connection:connecting` | `serverName?` | TCP connect |
| `connection:connected` | `serverId?` | Accepted |
| `connection:failed` | `message, allowReconnect?` | Could not reach |
| `connection:disconnected` | `message` | Dropped |
| `connection:progress` | `{ phase, downloading, progress, filesDownloaded, filesTotal, bytesDownloaded, bytesTotal, currentFile }` | Handshake / resource download |
| `console:push` | `{ level, category, message, timestamp }` | A log line (launcher/client backlog, then live) |
| `console:clear` | | `clear` / `cls` |
| `console:open` | `state` | Echo of `console:setState` / F8 |
| `console:commands` | `commands` | Built-in catalog for suggestions |
| `netgraph:update` | `{ fps, rtt, tx, rx, connected }` | Once a second while the HUD is on |

### UI -> Core

| Event | Arguments | What Core does |
|-------|-----------|----------------|
| `loaded` | | Version, devices, settings, command catalog, servers, invite, then `version:ready` |
| `connection:connect` | `host, port, cdn, auth, password, id, name, applyTheme` | Join |
| `connection:abort` | | `RequestDisconnect` |
| `connection:disconnect` | | `RequestDisconnect` |
| `console:execute` | `command` | Run a built-in client command |
| `console:setState` | `state` | Echo `console:open` and refocus CEF |
| `servers:reload` | | `servers:update` with `[]` |
| `settings:change` | `key, value` | Save; volume/voice apply live, display waits for next launch |
| `settings:devices:reload` | | Re-enumerate WASAPI endpoints |
| `settings:currentVolume:toggle` | `on` | Start or stop mic-test loopback |
| `settings:talk:toggle` | `on` | Latch PTT from the menu and close it |
| `ui:toggle` | `state?` | Cursor / menu focus. Offline, forced open |
| `netgraph:enable` | `visible` | Start or stop samples (does not persist; `settings:change` / `netgraph` does) |
| `exit` | | `PostQuitMessage` |

## Reserved (not implemented)

Connection stages: `connection:idle`, `joining`, `startingResources`,
`reloadingGameMap`, `preloadingMods`, `startingGame`,
`downloadingResources`, `validatingResources`, `downloadingRuntimes`,
`validatingRuntimes`, `inQueue`, `downloadingAdditionalResources`,
`validatingAdditionalResources`, `requestPermissions`,
`connection:accept`, `connection:deny`, `connection:reconnect`.

Console: `console:end`, `console:reset`, `console:forceTransparent`,
`console:openLogFile`.

Servers: `servers:updateError`, `servers:setPing`, `serverData:*`,
`servers:favorite:add/remove`. A masterlist is not wired; the browser
is an empty list.

Misc: `version:setRss`, `version:setManifest`, `ui:startupProgress`,
`ui:setWatermarkPosition`, `ui:setEarlyAuthState`, `ui:customLocales`,
`ui:resetSkin`, `keydown`.

## Example flow

```
UI  emit loaded
Core emit version:update         "0.1.0", "develop", "test", "", false, false
Core emit settings:devices:update { input: [...], output: [...] }
Core emit settings:update         { nickname, lastServer, netgraph, ... }
Core emit console:commands        [{ name, usage, hint, aliases, args }, ...]
Core emit servers:update          []
Core emit connection:invite       { address, name, ... }   (only if launched that way)
Core emit version:ready
Core emit ui:ready                                          (game finished starting)

UI  emit connection:connect  "127.0.0.1", 22005, "", "", "", "", "Player", false
Core emit connection:setServer        "127.0.0.1"
Core emit connection:connecting       "127.0.0.1:22005"
Core emit connection:progress         { phase: "downloading", progress: 0.4, ... }
Core emit connection:connected        "127.0.0.1"
Core emit ui:toggle                   false
```
