# GitHub Copilot — VantaMP

Read [`AGENTS.md`](../AGENTS.md) in the repository root before suggesting
edits. [`docs/README.md`](../docs/README.md) is the human map.

## Must follow

- Game types stay in `code/client/src/sdk/` files listed by
  `VANTAMP_CLIENT_SDK_FILES`. Do not include plugin-sdk above that wall.
- GTA addresses are signatures in `patterns.cpp`, never literals.
- Do not null-check `gPatterns.*` after init.
- `PlayerEntity` / `VehicleEntity` are append-only. Enter/exit is an RPC,
  not a snapshot flag.
- Do not write GTA outputs (`bIsDucking`, `m_fSteerAngle`, `isInAir`).
- **F** = driver / jack (seat 0). **G** = passenger. Never reassign a
  taken seat.
- `OpenDoor` only when the ratio moved more than 2/255. Driver owns the
  swing. Never max-merge door ratios.
- Build with `builds\build.bat` from `new/`. Client is 32-bit, server is
  64-bit. Do not invent ad-hoc CMake trees.

## Style

Match the file you are in: why-comments in caps for the non-obvious rule,
then the code. Prefer extending an SDK function over adding a new
translation unit to the plugin-sdk list.
