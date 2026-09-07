# VantaMP

A MafiaHub Framework multiplayer modification for **Grand Theft Auto: San Andreas**, targeting the
build distributed by the Rockstar Games Launcher.

Two clients connect, see each other, walk, jump, crouch, swim, shoot, talk, and share cars. Remote
players are input replay — GTA walks them from the owner's pad. Cars are occupancy RPCs plus
owner motion plus a pad swap for steering. **F** is driver / jack; **G** is passenger. Voice is
proximity on the same RakNet UDP. **V** is push-to-talk.

How that is put together, and the rules that keep it from fighting itself, live in
[`docs/`](docs/README.md). New clone: [`docs/setup.md`](docs/setup.md). Agents
read [`AGENTS.md`](AGENTS.md) (Claude: [`CLAUDE.md`](CLAUDE.md); Cursor rules in
[`.cursor/rules/`](.cursor/rules)).

## Get started

Windows, Visual Studio with the C++ workload, Python 3, Node.js, Git. A legal
GTA: San Andreas install is only needed to *run* a client.

```bat
git clone --recurse-submodules https://github.com/vanta-multiplayer/vanta-framework.git
cd vanta-framework
scripts\setup.bat
scripts\build.bat
```

`setup.bat` inits `vendors/plugin-sdk`, finds or clones the MafiaHub Framework,
and `npm install`s the interface. `build.bat` compiles the client, launcher and
server. Full notes: [`docs/setup.md`](docs/setup.md).

## Layout

| Path | Target | Arch | What it is |
|------|--------|------|------------|
| `code/launcher` | `VantaMPLauncher` | 32-bit | PE-loads `gta_sa.exe` into its own process and hands control to the client |
| `code/client` | `VantaMPClient` | 32-bit | The in-process mod DLL: sync, patches, UI |
| `code/server` | `VantaMPServer` | 64-bit | Replicas, reliable RPCs, scripting |
| `code/shared` | — | — | Entities, RPCs and rates compiled into both sides |
| `code/client/src/sdk` | — | — | The only place game types exist. Signatures, not addresses |
| `docs/` | — | — | Architecture, player sync, vehicles, voice, SDK, building, interface |
| `ui/` | — | — | Solid menu. Built into one HTML file and compiled into the client |
| `data/resources/freeroam` | — | — | Waterfront spawn and the dock Sultan |
| `vendors/plugin-sdk` | — | — | DK22Pac's plugin-sdk, for San Andreas **structure layouts** |

The game is a 32-bit binary, so the client and launcher build only in the 32-bit tree
while the server builds in the 64-bit one. From **this** repo:

```bat
scripts\build.bat
scripts\build.bat client
scripts\build.bat server
scripts\build.bat ui
```

Run `VantaMPServer.exe` from the framework `builds\build-64\bin`, then
`VantaMPLauncher.exe` twice from `builds\build\bin`. Kill the launchers (and
`gta_sa`) before a client rebuild — the DLL is locked. Full steps:
[`docs/building.md`](docs/building.md).

## How the game is launched

The title is resolved through `ProjectPlatform::ROCKSTAR`: the launcher reads the install root out
of the title's registry key (`HKLM\SOFTWARE\Rockstar Games\GTA: San Andreas`, `InstallFolder`), so
the Rockstar Games Launcher does not have to be running to find the game. A player who owns the
game elsewhere can still point the launcher at their own `gta_sa.exe` through the manual prompt.

RGL's build is wrapped by `MTLX.dll`: the executable's `.text` is encrypted on disk and the wrapper
only decrypts it for a process the Rockstar Games Launcher itself started. `useRockstarImageSnapshot`
handles that — the first run for a given build lets the launcher start the game once, captures the
decrypted code, and caches it in `cache/VantaMP_image_snapshot.bin`; every later run maps the image
and lays that code back over it. See `code/framework/src/launcher/README.md` for the details.

## Status

The launcher maps the game, the client attaches inside it, and two players can share a session.

- **On foot.** Input replay, interpolated heading (the +0x558 field, not the matrix), gated
  velocity, duck as a task, swim as the in-water task, nametags, weapons, shots, reloads, deaths.
- **In cars.** Script-spawned vehicles, F / G enter and exit, jacking the driver (including
  through the passenger door), door open ratios, damage from bystanders, steering from a pad
  swap rather than a written `m_fSteerAngle`.
- **Voice.** 40 m proximity on the game UDP. Devices, output gain and PTT live in Settings
  and `VantaMP.json`. Mic test is loopback on those endpoints and pauses the live session
  so the same WASAPI device can open twice.
- **Menu.** Solid page compiled into the DLL (`http://vanta/ui.html`). Tokens and logo
  match the public site. **Esc** closes it in-world; offline it stays up.
  **F8** / **~** is the console. Built-in commands (`help`, `connect`, `netgraph`, …)
  run in C++. `vanta://connect/host:port` and `--connect` open an invite card.
  Display changes save only — a live `Reset` on this exe crashes; the next launch
  creates the device.
- **Freeroam.** Waterfront spawn at `2899.41, -1672.64, 12.89` and a Sultan on the dock at
  `2865.93, -1665.88, 10.88`. Grove Street is commented beside the numbers for when the
  waterfront has been watched long enough.

A D3D9 device hook still drives `Core::Application` — renderer, ImGui and the client tick hang
off the game's own `EndScene`. The ped debug panel is still there.

**plugin-sdk** is vendored as a submodule for structure definitions. Its event and address tables
are for GTA:SA 1.0 US and bind to none of this build, so:

- structures come from plugin-sdk (`CPed`, `CVehicle`, … — layouts are the same across builds);
- addresses come from signature scans in `code/client/src/sdk/patterns.cpp`, derived with
  `scripts/derive_pattern.py` and verified at startup;
- anything plugin-sdk only *declares* is reimplemented locally behind an SDK function.

plugin-sdk is confined to the files named in `VANTAMP_CLIENT_SDK_FILES`. It ships its own
DirectX SDK headers that lose a fight with the Windows SDK's dxgi, and it needs `RW` defined,
which collides with a function of the same name in the framework's `jitasm.h`. The full rule
set is [`docs/sdk-and-patterns.md`](docs/sdk-and-patterns.md).
