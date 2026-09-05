# VantaMP

A MafiaHub Framework multiplayer modification for **Grand Theft Auto: San Andreas**, targeting the
build distributed by the Rockstar Games Launcher.

## Layout

| Path | Target | Arch | What it is |
|------|--------|------|------------|
| `code/launcher` | `VantaMPLauncher` | 32-bit | PE-loads `gta_sa.exe` into its own process and hands control to the client |
| `code/client` | `VantaMPClient` | 32-bit | The in-process mod DLL, loaded by the launcher as the mapped game starts |
| `code/server` | `VantaMPServer` | 64-bit | The dedicated server |
| `code/shared` | — | — | Constants and version data shared by all three |
| `vendors/plugin-sdk` | — | — | DK22Pac's plugin-sdk, for the San Andreas game structures |

The game is a 32-bit binary, so the client and launcher build only in the 32-bit tree
(`builds\build`) while the server builds in the 64-bit one (`builds\build-64`):

```bat
builds\build.bat VantaMPClient 32
builds\build.bat VantaMPLauncher 32
builds\build.bat VantaMPServer 64
```

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

The launcher maps the game, the client attaches inside it, and San Andreas runs under PE loading.
A D3D9 device hook drives `Core::Application` - the framework's renderer, ImGui and the client tick
all hang off the game's own `EndScene` - and the first feature on top of it is a live debug panel
showing the local ped's position, heading, model, ped type, health and vehicle.

**plugin-sdk** is vendored as a submodule for its San Andreas structure definitions. Its event and
address tables are for GTA:SA 1.0 US and bind to none of this build (0 of 19 of its GTASA hook sites
are call sites here), so:

- structures come from plugin-sdk (`CPed`, `CEntity`, `CPlaceable`, ... - layouts are the same
  across builds, which is why plugin-sdk itself only varies addresses between versions);
- addresses come from our own signature scans in `code/client/src/sdk/patterns.cpp`, validated at
  startup the way the rest of the MafiaHub projects do it;
- anything plugin-sdk only *declares* (its methods live at 1.0 US addresses) has to be reimplemented
  locally - `CPlaceable::GetHeading` is the first example.

plugin-sdk is confined to a single translation unit (`src/sdk/game.cpp`) which hands the rest of the
client a plain `LocalPedState`. It has to be: the SDK ships its own DirectX SDK headers that lose a
fight with the Windows SDK's dxgi, and it needs `RW` defined, which collides with a function of the
same name in the framework's `jitasm.h`.
