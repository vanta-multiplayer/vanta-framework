# Building and running

A first-time clone starts at [Setup](setup.md). After that, build from
**this** repo — `scripts\build.bat` finds the framework and forwards:

```bat
scripts\build.bat
scripts\build.bat client
scripts\build.bat launcher
scripts\build.bat server
scripts\build.bat ui
```

## Version, branch, environment

CMake stamps `VERSION` plus the GitHub branch and the channel into
`Version::rel` / `branch` / `environment`. Local builds read `git rev-parse
--abbrev-ref HEAD`. `develop` and feature branches are **test**; `release`
and `release/*` are **release**.

CI can pin them before configure:

```bat
set GITHUB_REF_NAME=develop
set VANTAMP_ENVIRONMENT=test
```

or pass `-DVANTAMP_GIT_BRANCH=develop -DVANTAMP_ENVIRONMENT=test` to the
CMake configure. The page shows `0.1.0 · test · develop`. The wire still
uses `rel` only, so a test and release build with the same number can
still talk.

The game is a 32-bit binary, so the client and launcher live in the 32-bit
tree and the server in the 64-bit one. Those trees are still the framework's:

| Binary | Path |
|--------|------|
| Server | `<Framework>/builds/build-64/bin/VantaMPServer.exe` |
| Client | `<Framework>/builds/build/bin/VantaMPLauncher.exe` |

The launcher PE-loads `gta_sa.exe` and hands control to `VantaMPClient.dll`.
Two copies of the launcher on one machine is the normal way to test
anything. Each instance writes `<Framework>/builds/build/bin/logs/VantaMP-<pid>.log`.

## The DLL is locked while a client runs

Kill these before a client rebuild, or the link fails with LNK1168:

- `VantaMPLauncher`
- `gta_sa`
- `cef_subprocess`

Restart the **server** as well whenever a `PlayerEntity` or `VehicleEntity`
field changed. The wire is positional. A mismatched pair deserializes
garbage and does not tell you.

## Finding the game

The title is `ProjectPlatform::ROCKSTAR`. The launcher reads
`HKLM\SOFTWARE\Rockstar Games\GTA: San Andreas` / `InstallFolder`, so the
Rockstar Games Launcher does not have to be running just to find the
install. A player who owns the game elsewhere can still point at their own
`gta_sa.exe`.

RGL's build is wrapped by `MTLX.dll`: `.text` is encrypted on disk and only
decrypts for a process the Rockstar Games Launcher itself started.
`useRockstarImageSnapshot` handles that — the first run for a given build
lets the launcher start the game once, captures the decrypted code, and
caches it in `cache/VantaMP_image_snapshot.bin`. Later runs map the image
and lay that code back over it. Details:
`code/framework/src/launcher/README.md` in the parent tree.

## Patterns

Signatures are derived and verified against a clean 1.0 US exe, then
resolved at startup against whichever build was mapped:

```
python scripts/derive_pattern.py <gta_sa.exe> 0xADDR
python scripts/verify_patterns.py <gta_sa.exe>
```

See [SDK and patterns](sdk-and-patterns.md).

## Interface

The page is compiled into the client. There is no `ui/` folder next to the
DLL and nothing to copy after a link. CEF opens `http://vanta/ui.html`.
**Esc** closes the menu, **F8** / **~** is the console, **V** is push-to-talk.

```
scripts\build.bat ui                # from this repo; needs node
scripts\build.bat client            # embeds data/ui/index.html (or ui/dist)
```

Details: [Interface](ui.md).

## What a two-client smoke test should show

1. Both players spawn at the waterfront and see nametags.
2. Walk, jump, crouch, swim — remotes stay on their feet and do not sit 90°
   off.
3. F into the dock Sultan (560 at `2865.93, -1665.88, 10.88`). The other
   client sees the walk-in and the drive.
4. G into a free passenger seat. The door they used swings on the other
   client.
5. F on an occupied driver seat jacks. G does not.
6. The driver can close a door. It stays shut, and it does not slam on a
   beat.
7. Hold **V** near the other player. They hear you. Forty metres away they
   do not. **F8**, type `help`, then `netgraph on`.
