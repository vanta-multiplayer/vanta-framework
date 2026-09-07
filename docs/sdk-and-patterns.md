# SDK and patterns

The SDK is the only place that may name a GTA type. Above it, the game is
player slots, `void *` handles, network ids and plain numbers.

```
sdk/game.h              umbrella — include a specific header if you only need one
sdk/game_sdk.h          the plugin-sdk wrapper
sdk/patterns.{h,cpp}    the signature table
sdk/game_build.{h,cpp}
sdk/input.{h,cpp}       pad, local read, remote write          [plugin-sdk]
sdk/camera.{h,cpp}      camera rotation, per slot
sdk/streaming.{h,cpp}   model request / residency / interiors  [plugin-sdk]
sdk/draw.{h,cpp}        nametag font
sdk/ped/access.h        INTERNAL raw ped access (incl. ReleaseRealTimeShadow)
sdk/ped/local_ped.*                                            [plugin-sdk]
sdk/ped/remote_ped.*                                           [plugin-sdk]
sdk/ped/weapon.*                                               [plugin-sdk]
sdk/ped/shot.*                                                 [plugin-sdk]
sdk/ped/aim.*           PointGunInDirection intercept
sdk/vehicle.*           create, pose, damage, door ratios      [plugin-sdk]
sdk/vehicle_occupancy.* enter / exit / jack tasks              [plugin-sdk]
```

`sdk/ped/` rather than `sdk/entities/`: `game/entities/` already exists
above the boundary and already has a `local_player.cpp`. The names would
collide for a reader and for object files.

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
   parent Vanta checkout).
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

## Provenance file

`scripts/vehicle_addresses.txt` is the batch list for the car and
occupancy signatures. Each line is `0xADDR Name`. The address is 1.0 US
ground truth for `derive_pattern.py`; the running client never reads this
file.
