# AGENTS.md

Guidance for any coding agent (Cursor, Claude, Codex, Gemini, Copilot, Aider,
Windsurf) working in **this** repository: `new/code/projects/vanta`.

The human docs in [`docs/`](docs/README.md) are the map. Code comments are why
a line exists. This file is what you must not break. Session notes in
[`.ai/HANDOFF.md`](.ai/HANDOFF.md) are a snapshot — prefer `docs/` and the
code when they disagree.

## Who you are talking to

norelock / rmru. Software engineer. Work in the code; do not narrate a plan
instead of editing. Do not commit unless asked. Do not push unless asked.
Do not edit a plan file.

## Where you are allowed to write

This git repo only. Do **not** edit:

- the Framework tree (`new/` outside this folder) — we consume it
- `current/` — old Vanta, reference only
- `reference/` — MTA and friends, reference only
- the user's game install, `user.cfg`, or anything outside the repo

Reading those trees is fine. Writing is not.

## Non-negotiable

### 1. Signatures, never addresses

Every GTA call is a signature in `code/client/src/sdk/patterns.*`. A comment
that names a 1.0 US address is **provenance**, not a value to use.

Derive against the clean exe, then verify the whole table:

```
python scripts/derive_pattern.py <gta_sa.exe> 0xADDR
python scripts/verify_patterns.py <gta_sa.exe>
```

Clean exe (outside this repo): `current/build/gta-clean/gta_sa.exe`.
Do not add a GTA call without a derived unique pattern.

### 2. Never null-check a resolved pattern

`hook::get_pattern` asserts on exactly one match during `Patterns` init. If
the mod is running, every `gPatterns.*` entry is valid. A zero test is dead
code that hides the real failure point.

```cpp
// FORBIDDEN
if (gPatterns.CCarCtrl__CreateCarForScript == nullptr) return;

// CORRECT
gPatterns.CCarCtrl__CreateCarForScript(...);
```

Guard real runtime conditions (a `CPed*` that may be null, a missing replica).
Never guard a fact.

### 3. The SDK is a wall

Game types (`CPed`, `CVehicle`, plugin-sdk headers) live in files named by
`VANTAMP_CLIENT_SDK_FILES` in `code/client/CMakeLists.txt`. Everywhere else
talks in slots, `void *` handles, network ids and plain numbers.

Do not widen that list to make a call site shorter. Put the call behind an
SDK function. plugin-sdk is header-only — its `.cpp` bodies are 1.0 US and
are not linked.

### 4. Append-only wire

`PlayerEntity` and `VehicleEntity` serialize positionally. Add fields at the
end. Restart the server after any field change — a mismatched pair
deserializes garbage silently.

Puresync never seats anyone. Enter / exit / jack / shot / reload / death
are reliable RPCs. A flag that plays once will restart; an unreliable
packet can be missed.

### 5. Do not write GTA outputs

These are recomputed. Writing them does nothing, or fights the task:

| Looks syncable | What you actually do |
|----------------|----------------------|
| `bIsDucking` | Run `CTaskSimpleDuck` |
| `m_fSteerAngle` | Swap the pad around vehicle `ProcessControl` |
| `bIsStanding` / `isInAir` | Gate velocity. Do not write the flag |
| `bSubmergedInWater` | The in-water **task** is running |

Read and write the **same** field. Heading is `m_fCurrentRotation` (+0x558),
not the matrix (that was the permanent 90° bug).

## Decisions — do not relitigate

- Remote players are **input replay**, not animation replication.
- Ped rotation **is** interpolated (plain lerp + wrap). Position uses the
  framework interpolator. Camera scripting packet (`iCamSync`) is
  deliberately not implemented.
- Jump is the controller button. Keep `OldState` as the previous frame.
  Do not port a one-shot jump task event.
- Duck is a **state** (`isDucked` → task). Drop the crouch button from the
  on-foot pad. In a car that same key is the horn — write `ShockButtonL`.
- A swimmer is not airborne. Velocity is gated: every update in the air,
  every fourth on the ground, only on an update.
- **F** = driver enter / exit / jack (always seat 0). **G** = passenger
  only. Never reassign a taken seat. Jack completes on `warp = true`.
- Door ratios: `OpenDoor` only when the delta is > 2/255. Driver owns the
  swing. Never max-merge ratios. `playSound=false` still slams.
- Sentry is wired and works. Leave it.

## Build

From **this** repo, never ad-hoc CMake. `scripts\build.bat` finds the
framework and forwards:

```bat
scripts\build.bat client       :: VantaMPClient 32
scripts\build.bat server       :: VantaMPServer 64
scripts\build.bat launcher     :: VantaMPLauncher 32
scripts\build.bat              :: all three, then pack
```

It also takes `ui`, `types`, `pack`, `protect`, `release`, and forwards any
`<target> <32|64>` pair to the framework script. The framework's own
`builds\build.bat` still works, but the wrapper is the entry point — it is
what handles framework discovery.

`protect` (Anti-Cheat catalog signing) is **not** in the default chain, and
Debug builds do not start Anti-Cheat at all — `EAC disabled: FW_DEBUG build`
in the log is expected. Use `scripts\build.bat release` when shipping.
See [`docs/eac.md`](docs/eac.md).

Kill `VantaMPLauncher`, `gta_sa`, `cef_subprocess` before a client link
(LNK1168, or LNK1201 on the PDB). Two launchers on one machine is the normal
test. Logs: `new/builds/build/bin/logs/VantaMP-<pid>.log`.

## Where to read first

| Task | Open |
|------|------|
| Layers, ownership, channels | [`docs/architecture.md`](docs/architecture.md) |
| Walk / aim / shoot / duck / swim | [`docs/player-sync.md`](docs/player-sync.md) |
| Cars, F/G, jack, doors, steer | [`docs/vehicles.md`](docs/vehicles.md) |
| Voice, PTT, mic test | [`docs/voice.md`](docs/voice.md) |
| Interiors / dimensions | [`docs/world-space.md`](docs/world-space.md) |
| Clock / weather / gravity / visuals | [`docs/world-env.md`](docs/world-env.md) |
| Easy Anti-Cheat (EOS) | [`docs/eac.md`](docs/eac.md) |
| Silent OpenID / Vanta Auth | [`docs/eos.md`](docs/eos.md) |
| Scripting IntelliSense (`@vantamp/types`) | [`docs/scripting-types.md`](docs/scripting-types.md) |
| Menu, console, `vanta://` | [`docs/ui.md`](docs/ui.md) |
| Core <-> page events | [`docs/events.md`](docs/events.md) |
| New GTA call or signature | [`docs/sdk-and-patterns.md`](docs/sdk-and-patterns.md) |
| Build and two-client run | [`docs/building.md`](docs/building.md) |
