# GEMINI.md

Guidance for Gemini CLI in this repository.

> **Read [`AGENTS.md`](AGENTS.md) first.** It is the source of truth.
> Human map: [`docs/README.md`](docs/README.md).

VantaMP is a GTA:SA multiplayer client + server. This directory is the
project git repo. Do not edit the Framework (`new/` outside this folder),
`current/`, or `reference/`.

```bat
builds\build.bat VantaMPClient 32
builds\build.bat VantaMPServer 64
```

Those commands run from `new/`. Kill running launchers before linking the
client DLL. Do not commit unless asked.
