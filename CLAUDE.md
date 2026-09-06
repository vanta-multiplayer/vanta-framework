# CLAUDE.md

Guidance for Claude Code (claude.ai/code) in this repository.

> **Read [`AGENTS.md`](AGENTS.md) before doing anything else.**
> Its "Non-negotiable" and "Decisions" sections are binding. They are not
> duplicated here.

## This repo

VantaMP — GTA:SA multiplayer on the MafiaHub Framework. This folder
(`new/code/projects/vanta`) is its own git repo. The Framework around it
is not ours to edit.

Human map: [`docs/README.md`](docs/README.md).

## Build

From `new/`, not from here:

```bat
builds\build.bat VantaMPClient 32
builds\build.bat VantaMPServer 64
```

Kill `VantaMPLauncher` / `gta_sa` / `cef_subprocess` before a client rebuild.

## Claude-specific

- Work in the code. Do not replace an edit with a long plan unless asked.
- Do not commit or push unless the user asks.
- Do not add plugin-sdk includes above the SDK wall.
- Do not invent GTA addresses. Derive a signature
  (`scripts/derive_pattern.py`) against `current/build/gta-clean/gta_sa.exe`.
- When the user says F they mean driver / jack. G is passenger only.
