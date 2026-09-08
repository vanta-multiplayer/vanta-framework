# VantaMP — Handoff

Durable notes for the next agent session. Kept short on purpose; the repo is the
source of truth whenever this disagrees with it.

## Project

GTA:SA multiplayer client + server ("VantaMP"), built on the MafiaHub Framework.

- Repo root for this project: `new/code/projects/vanta` (its own git repo, branch `develop`).
- The outer `new/` tree is the Framework + vendors, and is **not** ours to edit.
- `current/` at the workspace root is the OLD, previously-working Vanta
  implementation. It is a reference, not a build target, and it is frequently the
  fastest answer to "how should this work".
- MTA:SA source is checked out at `%TEMP%\mta-ref` (`C:\Users\norbe\AppData\Local\Temp\mta-ref`).
  It is the other reference and generally the authority on sync behaviour.

## Build and run

From this repo: `scripts\build.bat client` / `server` / `ui`. That
forwards into the Framework tree. Details: `docs/building.md`.

```powershell
cd new
cmd /c "builds\build.bat VantaMPClient 32"    # client DLL
cmd /c "builds\build.bat VantaMPServer 64"    # server
```

- Server: `new/builds/build-64/bin/VantaMPServer.exe`
- Client: `new/builds/build/bin/VantaMPLauncher.exe` (run it twice for two players)
- Logs: `new/builds/build/bin/logs/VantaMP-<pid>.log`, one per client instance.
- The DLL is locked while a client runs — kill `VantaMPLauncher`/`gta_sa` before rebuilding.
- **Restart the server whenever `PlayerEntity` / `VehicleEntity` / `WorldEnv`
  fields change.** The wire format is positional with no field names, so a
  mismatched pair deserializes garbage silently rather than failing.

## Architecture that matters

**Remote players are driven by input replay, not by animation replication.**
Each remote player gets a real `CPlayerPed` in its own player slot. The owning
client sends its controller state; the receiving client writes that into the
slot's pad and lets GTA's own `ProcessControl` walk the ped. Position sync only
corrects where that walk has drifted.

- `SwitchToRemote`/`SwitchToLocal` (`game/entities/remote_players.cpp`) swap the
  game's single-player globals (pad, `PlayerInFocus`, camera rotation) around each
  remote ped's `ProcessControl`.
- The per-ped correction runs from that same hook, **after** `ProcessControl` and
  **outside** the swap. Inside the swap it takes the process down with no
  exception to report.
- `sdk/` is a hard boundary: plugin-sdk and raw GTA pointers live there and
  nowhere else. `code/client/CMakeLists.txt` enforces it by naming the files
  that get the plugin-sdk include directories, in `VANTAMP_CLIENT_SDK_FILES`.
  A file not on that list cannot name a game type even by accident. Do not widen
  it — put the game call behind an SDK function instead. Its layout:

  ```
  sdk/game.h            umbrella; include a specific header if you only need one
  sdk/game_sdk.h        the plugin-sdk wrapper
  sdk/patterns.{h,cpp}  the signature table (included by ~13 files; leave put)
  sdk/game_build.{h,cpp}
  sdk/input/input.*     the pad, local read and remote write     [plugin-sdk]
  sdk/world/camera.*    camera rotation, per slot                 no plugin-sdk
  sdk/world/streaming.*                                           [plugin-sdk]
  sdk/world/draw.*
  sdk/world/environment.* WorldEnv apply (clock/speed/visuals)    no plugin-sdk
  sdk/ped/              local / remote / weapon / shot / aim
  sdk/vehicle/          create, RGB paint, occupancy              [plugin-sdk]
  ```

  Note `sdk/ped/` rather than `sdk/entities/`: `game/entities/` already exists
  above the boundary and had a `local_player.cpp` of its own, so the names would
  have collided both for a reader and for object-file naming.
- **World env** — server `WorldManager` + script `World.*` + client
  `PulseWorldEnv` every frame (speed/gravity/visuals). Docs:
  `docs/world-env.md`. Not the same as world-space (interior/dimension).
- plugin-sdk is used **header-only**. Its `.cpp` bodies are 1.0 US addresses that
  are not linked, so any game function must be resolved as a pattern and called
  through a function pointer. Virtuals are reached through the vtable.
- Every address is a **signature**, never a hardcoded address. Derive with
  `scripts/derive_pattern.py <gta_sa.exe> 0xADDR`, verify the whole table with
  `scripts/verify_patterns.py <gta_sa.exe>` (currently **126** signatures).
  Clean exe: `current/build/gta-clean/gta_sa.exe`.

## Decisions already made — do not relitigate

- **Vehicle colour is RGB, not a palette.** GTA stores four palette
  indexes. Mapping `Color.fromRGB` to the nearest of 128 is wrong — a
  script can ask for any RGB. The replica carries four `uint32`
  0x00RRGGBB values; the client writes them into
  `ms_vehicleColourTable[0..3]` around `CVehicle::SetupRender` and
  restores the table after. Do not go back to indexes.
- **Sentry is wired up and works** (DSN in `shared/constants.h`, client + server).
  Events reach the dashboard. The local crash report was kept alongside it: the
  local one reads live values, Sentry only sees what was published before the
  fault. Vanta's filter is installed *after* Sentry so it chains to crashpad and
  both reports are written.
- **Breadcrumbs** (stage, last patch/hook/native call/operation) publish to the
  reporter. Operations deliberately do not publish — too hot — so they reach the
  local report only. That asymmetry is documented in the code.
- **Camera packet (MTA's 500ms `iCamSync`) is deliberately NOT implemented.** MTA
  uses it to tell the *server* what a player is looking at, for scripting and
  interest management. It is never relayed to other clients and changes nothing
  about rendering. The camera value that matters — rotation — rides the per-tick
  path, where MTA puts it. Revisit only when there is a camera scripting API.
- **Ped rotation IS interpolated** — aimed on arrival, turned frame by frame
  across the send interval, which is `CClientPed::Interpolate`. This reverses an
  earlier decision, and the reason is worth knowing before reversing it back: it
  was correct to snap only while `HeadingOf` read the ped's matrix, because a
  correction was then measured against a value the game had not rebuilt yet and
  never converged. The 90° fix made the read honest and retired that reason.
  Use the framework interpolator for position only; rotation is a plain lerp
  with `OffsetRadians` for the wrap, as MTA does.
- **Being in water is the in-water task running**, not `bSubmergedInWater`
  (false on the surface) or `bTouchingWater` (true in the shallows). Both are
  flags that task maintains. `Internal::IsPedInWater` is the only place to ask.
- **A swimmer is not airborne.** `bIsStanding` is false for both, so `isInAir`
  has to exclude water explicitly, or velocity lands on the every-update branch
  and fights the swim task.
- **Velocity is gated, MTA's way**: every update while airborne, every fourth on
  the ground, and only on an update rather than every frame. A ped on the ground
  gets its move speed from its own walk animation; overwriting that with the
  owner's reading is movement counted twice, since the input replay is already
  carrying it. The old project skipped player velocity entirely for this reason.
- **Jump rides the controller button, not a task event.** The two references
  disagree here and the disagreement was informative — see the `OldState` note
  under completed work. Once pad edges work, MTA's button replay is correct and
  needs no packet type or task hook, so that is what we do. Do not port the old
  project's `PlayerTaskEvent` for jump; it was a workaround for the pad bug.
- **Duck is a state, not a button.** Synced from the `isDucked` flag as a task,
  and the crouch button is deliberately dropped from the replayed pad. Two
  mechanisms driving one pose makes the ped flicker.
- One log file per client instance, keyed by pid — two clients on one machine is
  the normal way to test anything.

## Earlier: player sync (still true)

The notes below are from the heading / duck / swim work. Do not relitigate
them. They are not "this session".

- Finished the **duck-as-a-task** work the previous session was cut off mid-way
  through. `bIsDucking` is an *output* the game recomputes, so writing it does
  nothing; the crouch now runs a real `CTaskSimpleDuck` as `TASK_SECONDARY_DUCK`
  and is ended with `MakeAbortable`, matching `CClientPed::Duck`. Four new
  signatures (`CTask::operator new`, duck ctor, `Set`/`GetTaskSecondary`).
- **Fixed a 90° heading bug — this was the big one.** `SetPedRotation` writes
  `m_fCurrentRotation` (+0x558) but `HeadingOf` read the heading back off the
  *matrix* (`atan2(-right.x, right.y)`), which is a quarter turn from that field.
  The sender reports its own facing through the same function, so every player
  announced a heading 90° from where they stood and every receiver wrote it
  straight into the ped's rotation: every remote player was drawn 90° off,
  permanently, with no drift to reveal it. `HeadingOf` now reads +0x558 — the
  same field that is written, which is what MTA does (`GetCurrentRotation` /
  `SetCurrentRotation`). Verified: target 32.0 / ped 32.0.
- **Re-aim on a cadence, not only on change.** The error compensation pays off
  the gap it measured and stops, so the residue left when a player stopped moving
  stayed for good (measured 0.28 units, frozen). Now re-aimed every
  `kPlayerMs` regardless, as MTA does. Verified: idle error 0.00.
- **Warp threshold wired up.** `Shared::Interpolation::WarpThresholdFor` existed
  but was never called. A gap beyond `(5 + 5×speed) × tick/100` now snaps instead
  of sliding the ped across the map. MTA's rule and numbers.
- **Removed `aimVertical` from the wire.** It had no source and no consumer —
  four bytes of zeros per tick that made aim look synced. See TODOs for what
  doing it properly needs.
- **Gated velocity** to MTA's rule, and moved it, health and the transform out of
  the per-frame path onto the update path where they belong. Velocity was being
  written 60 times a second from a 10Hz source, against a ped the game was
  already walking from replayed input.
- **Fixed remote players being unable to jump at all.** `WriteRemoteInput` ended
  with `pad->OldState = state` *after* building the new state, so old and new
  were identical on every frame. Every discrete action in GTA is an edge —
  `JumpJustDown` and its siblings are all `NewState && !OldState` — so none of
  them could ever fire: no jumping, no crouch toggle, no entering a vehicle. The
  buttons all arrived correctly and were all faithfully ignored, which is why it
  looked like an animation problem. `OldState` now holds the previous frame's
  state. Sampling at 60fps against 10Hz input is fine: the first frame of a held
  button is an edge and the rest are not, which is one jump per press.
  This is also why the old project concluded button replay was unworkable and
  moved jump to one-shot task events — it had the same bug (its own comment,
  "THIS IS THE WHOLE BUG", describes exactly this line).

- **Split `sdk/game.cpp`** (616 lines, five unrelated concerns) into the layout
  above. Pure code movement apart from one small cleanup: a `PedAt(slot)` helper
  replaced the `PlayerInfoAt` / `m_pPed` / null-check preamble that every remote
  accessor repeated. The `CPlaceable` and `CEntity` layout asserts moved into
  `ped/access.h`, so now every SDK translation unit re-checks them instead of
  whichever file happened to hold them.
- **Swimming confirmed working in real water**, which was the untested risk. The
  raw 24-byte `CTaskComplexInWater` allocation has not crashed a client. In
  practice the receiving game usually starts the task itself before we get
  there — `swim 0/s` is the normal reading, not a fault — because a ped whose
  position is synced really is in the water.
- **Fixed the swim warp, which was two separate faults.** First, `isInAir` was
  true for the whole time a ped was in water, putting velocity on the
  every-update branch: the owner's move speed was written over GTA's swim task
  ten times a second, the same movement counted twice. Now 2–3/s, the ground
  rule. Second, and what was actually visible, heading was snapped once per
  update while the game turned the ped in between, so a turn stepped round in
  tenths. Water exposed it because the swim animation yaws the whole body with
  no footfalls to hide the seam.

## Current state

Two clients connect, see each other, walk, jump, crouch, swim, shoot, talk
and share cars. Interior and dimension are on the snapshot (MTA-shaped).
`player.interior` / `player.dimension` and the `Vehicles.*` helpers write
them; a different dimension streams the ped and car out.

Vehicle paint is **four RGB colours**, not the 128-entry GTA palette.
`car.setColor(Color.fromRGB(r,g,b), …)` is the real colour; the client
rewrites `ms_vehicleColourTable` around `CVehicle::SetupRender`. Paintjob
and the 17 upgrade slots (including wheels) ride the same replica. The
owner never writes cosmetics back from GTA. Models/upgrades are
allow-listed by `catalog/gta_sa.models`. Restart the server after this
field append.

Version stamps `rel`, GitHub `branch` and `environment` (`test` on
develop / features, `release` on `release` and `release/*`). Shown as
`0.1.0 · test · develop`. The wire still uses `rel` only.

**Menu / console.** Solid page compiled into the DLL. Esc closes the
menu, F8/~ console, V talk. Commands run in C++ (`nui_console.cpp`). `vanta://` and
`--connect` open an invite card. Netgraph is off until asked and saved
in `VantaMP.json`. Display changes save only — live Reset crashes.

**Voice.** 40 m proximity on the game UDP. Devices and PTT persist.
Mic test pauses the live session. In a car the listener stays on the
ped, not the chase cam.

**Disconnect.** Remote ped / vehicle pool-free must call
`ReleaseRealTimeShadow` first or the next shadow return AVs.

**The spawn is still a testing spawn** at the Los Santos waterfront so
both players start near deep water. Grove Street is commented in
`shared/world/spawn_points.h`.

**Temporary telemetry may still be in `player_sync.cpp`** (`TEMPORARY
TELEMETRY`). Strip it once sync has been watched long enough.

Human map: `docs/`. Prefer that folder when this file and the code
disagree.

## Known issues / not yet verified

- **Camera rotation is still snapped** at 10Hz, the way heading used to
  be. MTA lerps it in the same window as rotation and we do not. It is
  applied in `ApplyRemoteState`, not after `CorrectRemotePlayer`, so it
  needs its own lerp. First suspect if a turning seam remains.
- Masterlist / server browser is empty. Recent and favorites are
  emitted as `[]`.
- `isInAir` is sent and deliberately not applied — see the TODOs.

Weapons, ammo and reload **are** on the wire now. The old "invisible
gun" note is stale.

## TODOs, roughly in order

1. Strip the `SyncDbg` telemetry from `player_sync.cpp` once sync has
   been watched for long enough, and put the Grove Street spawn back.
2. Vertical aim. Needs `CShotSyncData::m_fArmDirectionY`, whose offset
   must be derived (plugin-sdk does not describe it), and it will likely
   need the same treatment as `cameraRotation` — held per slot and
   swapped in around the ped's processing.
3. Ledge-grab, if it turns out to look wrong. Ordinary climbs ride the
   jump button; only *hanging* needs seeding (`hangingDuringClimb` →
   `CTaskSimpleClimb` at `CLIMB_GRAB`). Costs a wire bit.
4. A real server browser / masterlist. The UI is ready; core emits `[]`.
5. `isInAir` is intentionally not applied to the ped — it is an output
   the game recomputes. It exists only to gate velocity. Leave it that
   way.

## Files currently in play

- `code/client/src/core/nui/` — public API in `nui.cpp`; settings,
  console, launch, netgraph, handlers are separate TUs.
- `ui/src/` — Solid menu. Tokens in `tailwind.css`. Commands in
  `lib/commands.ts` (suggestions only).
- `code/client/src/core/voice_io.*` — devices, mic test, PTT.
- `code/client/src/core/client_config.*` — `VantaMP.json`, including
  `netgraph`.
- `code/client/src/sdk/ped/access.h` — `ReleaseRealTimeShadow` before
  any pool-free.
- `code/client/src/game/sync/player_sync.cpp` — send/apply, interpolation.
- `code/shared/entity/player_entity.{h,cpp}` — the wire format. **Append only.**
  Interior and dimension are at the end. `shared/world/world_space.h`,
  `shared/rpc/world_space.h`.
- `code/shared/entity/vehicle_entity.{h,cpp}` — append-only after interior /
  dimension: paintjob, 17 upgrades, four `uint32` RGB colours.
  `shared/vehicle/colors.h`, `shared/vehicle/upgrades.h`,
  `shared/catalog/model_catalog.*`.
- `code/server/src/scripting/player.*` — Framework Player plus spawn /
  world-space. `vehicle.*` is the Entity handle. `bindings.cpp` only
  Bind + Register.

## Lessons worth keeping

- **Measure before theorising.** The 90° bug was found in one instrumented run
  after several sessions of reasoning about interpolation had missed it entirely.
  Log target-vs-actual for the quantity being synced; the number tells you.
- A flag the game *recomputes* cannot be synced by writing it. Duck, in-air and
  in-water are all outputs. Run the task instead.
- Read and write the same field. Most of the sync bugs so far have been a value
  going out through one accessor and coming back through another.
- **A workaround outlives the reason for it.** Rotation was snapped to dodge a
  read that has since been fixed, and the comment explaining why kept the
  workaround alive long after it had become the bug. When a note says "must
  not", check whether what it describes is still true.
- Symptoms surface where nothing hides them. The heading snap existed on land
  the whole time and only became visible in water, because a swim turn has no
  footfalls to mask the step. "It only happens while X" usually means X is the
  quiet case, not the broken one.
- Batch verification into one game launch. Rebuild-and-relaunch per hypothesis is
  the expensive part of this loop.
