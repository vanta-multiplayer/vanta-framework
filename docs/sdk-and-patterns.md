# SDK and patterns

The SDK is the only place that may name a GTA type. Above it, the game is
player slots, `void *` handles, network ids and plain numbers.

```
sdk/game.h                 umbrella — include a specific header if you only need one
sdk/game_sdk.h             the plugin-sdk wrapper
sdk/patterns.{h,cpp}       the signature table
sdk/game_build.{h,cpp}

sdk/ped/access.h           INTERNAL raw ped access (incl. ReleaseRealTimeShadow)
sdk/ped/local_ped.*                                               [plugin-sdk]
sdk/ped/remote_ped.*                                              [plugin-sdk]
sdk/ped/weapon.*                                                  [plugin-sdk]
sdk/ped/shot.*                                                    [plugin-sdk]
sdk/ped/aim.*              PointGunInDirection intercept

sdk/vehicle/vehicle.*      create, pose, damage, RGB paint        [plugin-sdk]
sdk/vehicle/occupancy.*    enter / exit / jack tasks              [plugin-sdk]

sdk/world/streaming.*      model request / residency / interiors  [plugin-sdk]
sdk/world/camera.*         camera rotation, per slot
sdk/world/draw.*           nametag font
sdk/world/environment.*    WorldEnv apply (clock, speed, visuals) — no plugin-sdk

sdk/input/input.*          pad, local read, remote write          [plugin-sdk]
```

`sdk/ped/` rather than `sdk/entities/`: `game/entities/` already exists
above the boundary and already has a `local_player.cpp`. The names would
collide for a reader and for object files. Vehicles sit next to peds
under `sdk/vehicle/`. World helpers (stream, camera, draw, environment)
are `sdk/world/`. `environment.cpp` is **not** on
`VANTAMP_CLIENT_SDK_FILES` — it only pokes pattern addresses, no game
types.

Vanta pool-deletes peds and vehicles without running `~CPhysical`. That
destructor is what returns a realtime shadow to the manager. Call
`Internal::ReleaseRealTimeShadow` **before** the pool-free or the manager
keeps `m_pOwner` and the next `ReturnRealTimeShadow` writes through a
freed pointer (AV at `0x705B3B` a beat after disconnect).

## Adding a game call

1. Find the 1.0 US address in plugin-sdk or the old Vanta table.
2. Derive a unique signature against the **clean** exe:

   ```
   python scripts/derive_pattern.py current/build/gta-clean/gta_sa.exe 0x00650BB0
   python scripts/derive_pattern.py current/build/gta-clean/gta_sa.exe --batch scripts/vehicle_addresses.txt
   ```

   The workspace-relative clean exe is
   `current/build/gta-clean/gta_sa.exe` (outside this git repo, in the
   parent Vanta checkout). If that checkout is not present, any clean
   **1.0 US** `gta_sa.exe` works — `derive_pattern.py` proves the signature
   resolves back to the address it started from, so a wrong build fails
   loudly rather than producing a bad pattern.
3. Add the member to `Patterns`, the scan to `patterns.cpp`, and a comment
   that names the 1.0 US address as **provenance**, not as a value the
   client uses.
4. Verify the whole table:

   ```
   python scripts/verify_patterns.py current/build/gta-clean/gta_sa.exe
   ```

5. Call it through a function pointer or a vtable slot from an SDK file
   that is already on `VANTAMP_CLIENT_SDK_FILES`. Prefer extending an
   existing function over adding a new `.cpp` to that list.

`hook::get_pattern` asserts on exactly one match. A signature that went
stale or became ambiguous fails at startup, not later, and not as a
corrupt write.

A signature that cannot be made unique is a judgement call (a nearby
call, a string reference), not something to wildcard until it "works".

## plugin-sdk

Vendored as a submodule for **structure layouts**. `CPed`, `CEntity`,
`CPlaceable`, `CVehicle` and friends are the same across builds, which is
why plugin-sdk itself only varies addresses between versions.

Its event tables and method bodies are GTA:SA 1.0 US. They bind to none of
the Rockstar Games Launcher build. Anything plugin-sdk only *declares* has
to be reimplemented locally — `CPlaceable::GetHeading` was the first
example; vehicle create / enter / `OpenDoor` are the same idea.

plugin-sdk is confined to the files on `VANTAMP_CLIENT_SDK_FILES` because
it ships its own DirectX SDK headers (they lose to the Windows SDK's dxgi)
and it needs `RW` defined, which collides with a function of the same name
in the framework's `jitasm.h`.

## Ped tasks

Two mistakes in this area have each cost a session. Both are about writing
to the task manager instead of asking GTA to do the work.

**The slots are not interchangeable.** `ePrimaryTasks` is
`PHYSICAL_RESPONSE 0`, `EVENT_RESPONSE_TEMP 1`, `EVENT_RESPONSE_NONTEMP 2`,
`PRIMARY 3`, `DEFAULT 4`. `CPedIntelligence::FlushImmediately(true)` puts
its replacement task in slot **4**, not the slot you just emptied — so
`SetTask(nullptr, PRIMARY)` followed by a flush leaves slot 3 empty. It
also wipes the event group and event handler
(`PedIntelligence.cpp:479-481`), which is what actually freezes a player:
input arrives as events, and the handler that turns them into tasks is
gone.

**Never null a live slot to cancel something.** Call `MakeAbortable` and
let the task unwind — that is MTA's pattern at every abort site
(`CClientPed.cpp:7798-7841`). The unwind is not decoration: for an enter
task it is what closes the door. Deleting the task skips it.

Abort **once**. `MakeAbortable` does not remove the task, it asks it to
unwind, which takes several frames — during which the ped still reads as
"entering". Anything that polls that condition will call straight back in
and restart the unwind from the top, which looks like a stuttering,
half-played animation. Latch on the *task pointer*, not a bool, so a
genuinely new task later is still abortable.

**Find a task by walking, not by guessing the slot.** An event's response
lands in `PHYSICAL_RESPONSE` or one of the event-response slots, never
`PRIMARY`, and it is usually a complex task with the real work nested
underneath. Scan every primary slot and follow `GetSubTask` (vtable slot 2,
after the destructor and `Clone`). MTA reads both ends the same way
(`CClientPed.cpp:7194`).

**Prefer raising GTA's own event.** Reconstructing a response by hand means
reproducing every step *and* the parts nobody has reversed. Adding a real
event to `ped->m_pIntelligence->m_eventGroup` runs the lot. Note that
`CEventGroup::Add` only *queues* — the task does not exist until the ped's
next `ProcessEvents`, so anything that waits on the task needs to cover
those frames — and it returns `nullptr` when `AffectsPed` refuses, which is
worth checking rather than assuming.

## Provenance file

`scripts/vehicle_addresses.txt` is the batch list for the car and
occupancy signatures. Each line is `0xADDR Name`. The address is 1.0 US
ground truth for `derive_pattern.py`; the running client never reads this
file.
