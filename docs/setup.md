# Setup for a new clone

This repository is a **MafiaHub Framework project**. CMake only loads it from

```
<Framework>/code/projects/vanta
```

If you already have that layout (this is how the project is developed), setup
only checks tools, inits the plugin-sdk submodule, and installs UI packages.
If you cloned or forked **just this repo**, setup clones the framework beside
it and junctions this checkout into `code/projects/vanta`.

## What you need

| Tool | Why |
|------|-----|
| Windows | The client is a 32-bit DLL inside `gta_sa.exe` |
| Visual Studio 2022, Desktop development with C++ | MSVC + the CMake/Ninja that `builds\build.bat` uses |
| Git | This repo and `vendors/plugin-sdk` |
| Python 3 | Embed the interface, derive/verify signatures |
| Node.js + npm | `ui/` (Solid). A client can still link against the last `data/ui/index.html` |
| GTA: San Andreas (legal install) | To *run* a client, not to compile |

## Commands

```bat
git clone --recurse-submodules <this-repo>
cd <this-repo>
scripts\setup.bat
scripts\build.bat
```

Point setup at a framework you already have:

```bat
scripts\setup.bat --framework D:\src\Framework
```

Or set `VANTA_FRAMEWORK` / `VANTA_FRAMEWORK_URL` before running it.

`setup.bat` writes `.framework-root` (gitignored) so later `scripts\build.bat`
calls find the same tree.

## After setup

```bat
scripts\build.bat            client + launcher + server
scripts\build.bat client
scripts\build.bat launcher
scripts\build.bat server
scripts\build.bat ui         rebuild and embed the page
scripts\build.bat pack       tester tree (runtime binaries only)
scripts\build.bat help
```

Binaries land in the **framework** build trees, not in this repo:

| Binary | Path |
|--------|------|
| Server | `<Framework>/builds/build-64/bin/VantaMPServer.exe` |
| Client | `<Framework>/builds/build/bin/VantaMPLauncher.exe` |

How to run two clients, and why the DLL locks: [Building](building.md).
