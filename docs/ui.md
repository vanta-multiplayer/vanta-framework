# Interface

The connect menu, console and netgraph are a Solid app. They are **not** a
folder next to the game. They are one HTML file, compiled into
`VantaMPClient.dll`, and served from memory at **`http://vanta/ui.html`**.

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

## Source

| Path | What it is |
|------|------------|
| `ui/` | Copy of `current/typescript/ui` (Solid + Vite + Tailwind) |
| `ui/src/lib/vanta.ts` | In-page event library (`emit` / `on`) |
| `ui/index.html` | Shell |
| `scripts/build_ui.py` | `npm run build` then embed |
| `scripts/embed_ui.py` | HTML bytes → C++ header |
| `data/ui/index.html` | Last built page. Fallback so a client compile does not need node |
| `code/client/src/core/nui/` | C++ side: serve `http://vanta/`, emit/on, connect menu |

## Rebuild the page

From this repo:

```
python scripts/build_ui.py
```

Or from the framework root, after CMake has seen the target:

```
builds\build.bat VantaMPUi 32
```

Then rebuild the client. Node is only required for that step. Linking a
client that already has `data/ui/index.html` just embeds that file.

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

## Talking to C++

The page uses `ui/src/lib/vanta.ts` (`emit` / `on`, positional args).
In the client that library talks to C++ at `http://vanta/emit` and
`http://vanta/events`. Resource scripts still use `Web.emit` / `Web.on`
on their own views.

Event tables: [`events.md`](events.md).
