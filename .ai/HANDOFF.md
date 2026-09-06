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

```powershell
cd new
cmd /c "builds\build.bat VantaMPClient 32"    # client DLL
cmd /c "builds\build.bat VantaMPServer 64"    # server
```

- Server: `new/builds/build-64/bin/VantaMPServer.exe`
- Client: `new/builds/build/bin/VantaMPLauncher.exe` (run it twice for two players)
- Logs: `new/builds/build/bin/logs/VantaMP-<pid>.log`, one per client instance.
- The DLL is locked while a client runs — kill `VantaMPLauncher`/`gta_sa` before rebuilding.
- **Restart the server whenever `PlayerEntity` fields change.** The wire format is
  positional with no field names, so a mismatched pair deserializes garbage
  silently rather than failing.

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
  sdk/input.{h,cpp}     the pad, local read and remote write     [plugin-sdk]
  sdk/camera.{h,cpp}    camera rotation, per slot                 no plugin-sdk
  sdk/ped/access.h      INTERNAL raw ped access shared by the two below
  sdk/ped/local_ped.{h,cpp}                                       [plugin-sdk]
  sdk/ped/remote_ped.{h,cpp}                                      [plugin-sdk]
  ```

  Note `sdk/ped/` rather than `sdk/entities/`: `game/entities/` already exists
  above the boundary and had a `local_player.cpp` of its own, so the names would
  have collided both for a reader and for object-file naming.
- plugin-sdk is used **header-only**. Its `.cpp` bodies are 1.0 US addresses that
  are not linked, so any game function must be resolved as a pattern and called
  through a function pointer. Virtuals are reached through the vtable.
- Every address is a **signature**, never a hardcoded address. Derive with
  `scripts/derive_pattern.py <gta_sa.exe> 0xADDR`, verify the whole table with
  `scripts/verify_patterns.py <gta_sa.exe>` (currently **126** signatures).
  Clean exe: `current/build/gta-clean/gta_sa.exe`.

## Decisions already made — do not relitigate

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
- **Ped rotation is not interpolated.** Applied outright, as MTA does. Its own
  turn animation is what makes it smooth.
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

## Completed this session

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

## Current state

Two clients connect, see each other, and track: position error 0.00 idle and
~0.6 worst while moving, heading exact when steady. No crashes. Client and server
both build clean.

**Temporary telemetry is still in `player_sync.cpp`** — a `Telemetry` struct,
`ReportTelemetry()`, and a `SEND`/`RECV` line per second under the `SyncDbg`
logger. It was what found the 90° bug. Remove it once the visual result is
confirmed; it is marked `TEMPORARY TELEMETRY` at each site.

## Known issues / not yet verified

- Whether a remote player visibly **walks** rather than glides has still never
  been confirmed by eye with two clients. That is the real test of the input
  relay design.
- Heading lags by up to one update during a fast turn (~25° observed). MTA has
  the same property. Only worth fixing if it reads badly.
- A remote player's **held weapon is invisible** — weapon type and ammo are not
  on the wire at all.
- Swimming is implemented but has **never been tested in actual water**. It is
  the riskiest code here: `CTaskComplexInWater` is absent from plugin-sdk, so the
  task is allocated raw at 24 bytes (12 of layout, 12 of MTA's safety slack) from
  the game's own task pool. If a remote player entering water crashes a client,
  start there.
- `isInAir` is sent and deliberately not applied — see the TODOs.
- Nothing is committed yet this session — working tree is dirty.

## TODOs, roughly in order

1. Confirm with two clients that remote peds now walk, jump once per press, and
   crouch. Then strip the telemetry and commit.
2. Weapon type + ammo on the wire, so a remote player's weapon is visible.
3. Vertical aim. Needs `CShotSyncData::m_fArmDirectionY`, whose offset must be
   derived (plugin-sdk does not describe it), and it will likely need the same
   treatment as `cameraRotation` — held per slot and swapped in around the ped's
   processing, since a value written to a ped directly is recomputed from the
   local camera on the next frame.
4. Ledge-grab, if it turns out to look wrong. Ordinary climbs ride the jump
   button and need nothing; only *hanging* needs seeding, via MTA's
   `hangingDuringClimb` flag → `CTaskSimpleClimb` at `CLIMB_GRAB`
   (`0x67A110`, with `TestForClimb` at `0x6803A0`). Costs a wire bit.
5. `isInAir` is intentionally not applied to the ped — it is an output the game
   recomputes, and MTA does not apply its equivalent either. It exists only to
   gate velocity. Leave it that way.

## Files currently in play

- `code/client/src/game/sync/player_sync.cpp` — send/apply, interpolation, warp,
  telemetry. Most of the active work is here.
- `code/client/src/sdk/ped/` — `access.h` holds `HeadingOf` and `SetPedRotation`;
  `remote_ped.cpp` holds `SetRemotePedDucked` and the ped lifecycle.
- `code/client/src/sdk/input.cpp` — `WriteRemoteInput` and its pad edge handling.
- `code/client/src/sdk/patterns.cpp` / `patterns.h` — the signature table.
- `code/client/src/game/entities/remote_players.cpp` — context swap + correction hook.
- `code/client/src/game/patches/input.cpp` / `input.h` — focus and input suppression.
- `code/shared/player_entity.{h,cpp}` — the wire format. **Append only.**
- `code/shared/sync_rates.h` — tick rate and warp threshold constants.

## Lessons worth keeping

- **Measure before theorising.** The 90° bug was found in one instrumented run
  after several sessions of reasoning about interpolation had missed it entirely.
  Log target-vs-actual for the quantity being synced; the number tells you.
- A flag the game *recomputes* cannot be synced by writing it. Duck, in-air and
  in-water are all outputs. Run the task instead.
- Read and write the same field. Most of the sync bugs so far have been a value
  going out through one accessor and coming back through another.
- Batch verification into one game launch. Rebuild-and-relaunch per hypothesis is
  the expensive part of this loop.
