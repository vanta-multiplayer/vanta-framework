# Interface

The connect menu, console, netgraph and watermark are a Solid app. They
are **not** a folder next to the game. They are one HTML file, compiled
into `VantaMPClient.dll`, and served from memory at **`http://vanta/ui.html`**.

```
ui/src/*.tsx
        │  npm run build  (vite-plugin-singlefile)
        ▼
ui/dist/index.html          one file, fonts and CSS inlined
        │  scripts/embed_ui.py
        ▼
generated/ui_content.hpp    byte array, compiled into the DLL
        │  RegisterEmbeddedInterface
        ▼
http://vanta/ui.html        CEF loads this, not file://
```

`file://` is an opaque origin. Chromium drops the CEF process messages the
page needs. A folder of files can also be edited, replaced or go missing.
Compiled in, it cannot.

The look follows the public site: ink ground `#090806`, amber `#f28b3a`,
Space Grotesk for display, Manrope for the rest. The logo is
`ui/src/assets/vanta-logo.png`. Fonts are bundled — CEF has no network
for a CDN.

## Source

| Path | What it is |
|------|------------|
| `ui/` | Solid + Vite + Tailwind |
| `ui/src/lib/vanta.ts` | In-page event library (`emit` / `on`) |
| `ui/src/lib/commands.ts` | Fallback catalog + tab suggestions |
| `ui/src/lib/cn.ts` | `clsx` + `tailwind-merge` |
| `ui/index.html` | Shell |
| `scripts/build_ui.py` | `npm run build` then embed |
| `scripts/embed_ui.py` | HTML bytes → C++ header |
| `data/ui/index.html` | Last local build (gitignored). Fallback so a client compile does not need node |
| `code/client/src/core/nui/` | C++ side: serve `http://vanta/`, emit/on, commands |

## C++ modules

`nui.cpp` is the public surface (`nui.h`). Everything else is a job:

| File | Owns |
|------|------|
| `nui.cpp` | Bring-up, visibility, escape / menu / console, connection reports, mic tick |
| `nui_state.cpp` | Shared flags, view lookup, focus |
| `nui_settings.cpp` | Current settings, video modes, apply-change |
| `nui_console.cpp` | Log tail, command table, `ExecuteClientCommand` |
| `nui_launch.cpp` | `vanta://`, `--connect`, HKCU protocol |
| `nui_netgraph.cpp` | `TickNetgraph` (sample only when the HUD is on and in-world) |
| `nui_handlers.cpp` | Page event bindings + startup emit |
| `internal.h` | Shared decls |

Do not grow `nui.cpp` back into a kitchen sink. Add a module.

## Rebuild the page

From this repo:

```
scripts\build.bat ui
```

Or from the framework root, after CMake has seen the target:

```
builds\build.bat VantaMPUi 32
```

Then rebuild the client. Node is only required for that step. Linking a
client that already has a local `data/ui/index.html` just embeds that file.

## How the URL is served

`RegisterEmbeddedInterface` (after CEF is up, before `CreateView`):

1. Puts the bytes in a `MemoryProvider` as `ui.html` and `index.html`.
2. Registers host `vanta` on the framework scheme → `fw://vanta/ui.html`.
3. Registers a scheme-handler factory for **`http` + host `vanta`** →
   `http://vanta/ui.html`. A miss is a real 404, not a DNS lookup.

The view opens `http://vanta/ui.html`. Same shape old Vanta used
(`http://vanta/index.html`); the name is `ui.html` because that is what
was asked for, and `/` / `index.html` alias it.

The framework still owns `fw://`. We do not add a custom scheme to
`cef_subprocess`. `http://vanta/` is an intercept of the built-in http
scheme for one host, which is what the old client did.

The overlay is click-through except where the page paints UI. Native
`<select>` popups do not work in this OSR CEF — keep the custom
`Dropdown`.

## Hotkeys

CEF drops keys once the view loses focus, so these are handled in the
window proc (`core/ui/window_input.cpp`):

| Key | Action |
|-----|--------|
| **Esc** | Close the console first, then the menu / back. Offline the menu stays open |
| **F8** / **~** | Toggle the console |

**V** is PTT and is not a menu key.

## Console commands

The page never interprets commands. It emits `console:execute`; C++ runs
them and pushes lines back. On `loaded`, core emits `console:commands`
so suggestions match the table. `ui/src/lib/commands.ts` is only a
fallback if that event has not arrived.

| Command | Aliases | What it does |
|---------|---------|--------------|
| `help` | `?` | List commands |
| `clear` | `cls` | `console:clear` |
| `connect <host[:port]>` | | Join. In-world: disconnect, then reconnect |
| `disconnect` | `dc` | Leave |
| `reconnect` | | Last server from `VantaMP.json` |
| `quit` | `exit`, `q` | `PostQuitMessage(0)` |
| `name <nick>` | `nick` | Save nickname |
| `netgraph [on\|off]` | `shownetgraph`, `togglenetgraph` | Toggle + persist |
| `status` | | Version, name, server, in-world, netgraph |
| `version` | | Version string |

Tab / arrows walk suggestions. Escape dismisses the list first.

## Launch invites

First UI start writes `HKCU\Software\Classes\vanta` so `vanta://` opens
the launcher.

```
vanta://connect/host:port?name=...&banner=...
VantaMPLauncher.exe --connect host:port
```

Query keys the card understands: `name`, `language`, `gamemode`,
`website`, `description`, `banner`, `avatar`, `tags`, `players`,
`maxPlayers`, `ping`. Core emits `connection:invite` and the page shows
`InviteCard` on the left, with the main menu hidden so the game shows
through.

Join from recent / favorites / the browser uses `JoinModal` (banner,
avatar, Play, name, password). Direct connect stays in the nav. Nickname
is Settings + that modal, not a home widget.

## Netgraph and watermark

Netgraph is **off** until asked (`netgraph` in `VantaMP.json`). The HUD
is still the page (`Netgraph.tsx`). C++ only samples when it is on, the
view is visible, and the player is in-world — once a second. FPS is
smoothed every frame; pushing a sample every `PostUpdate` flickered.

In-world, with the menu closed, the top-right watermark is the gray
logo, `vanta.mp`, and `version · environment · branch` (for example
`0.1.0 · test · develop`). The nav badge is hidden on the release
channel.

## Display settings

Width / height / borderless / exclusive are **saved only**.
`SetVideoMode` and a homemade `Reset` both crash on this exe when the
back-buffer size changes. The next launch reads `VantaMP.json` and
creates the device there. First launch still uses GTA's Device Selection
dialog (`display_setup_done`).

## Talking to C++

The page uses `ui/src/lib/vanta.ts` (`emit` / `on`, positional args).
In the client that library talks to C++ at `http://vanta/emit` and
`http://vanta/events`. Resource scripts still use `Web.emit` / `Web.on`
on their own views.

Event tables: [`events.md`](events.md).
