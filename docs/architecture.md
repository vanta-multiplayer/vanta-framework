# Architecture

VantaMP is a MafiaHub Framework project: a 32-bit client that lives inside
`gta_sa.exe`, a 32-bit launcher that PE-loads that exe, and a 64-bit dedicated
server. Shared types are compiled into both sides so the wire cannot drift.

```
code/launcher    VantaMPLauncher   32-bit   maps the game, injects the client
code/client      VantaMPClient     32-bit   sync, patches, UI, SDK
code/server      VantaMPServer     64-bit   replicas, RPCs, scripting
code/shared      —                 —        entities, RPCs, rates
code/client/src/sdk                —        the only place game types exist
data/resources/freeroam            —        spawn + the dock Sultan
```

## Layers on the client

```
game/patches     one-shot changes to the running game (keys, population, boot)
game/entities    player slots, local player, remote player lifecycle
game/sync        the per-tick loop: read local, apply remote
core/nui         CEF view. Page lives in the DLL at http://vanta/ui.html
core/voice_io    WASAPI devices, mic test, PTT latch
sdk/             GTA pointers, tasks, pads, vehicles
shared/          the same structs the server serializes
ui/              Solid source for that page (not shipped as files)
```

`core/nui/` is split by job (`nui.cpp` is the public API; settings, console,
launch, netgraph and handlers are their own files). See [Interface](ui.md).
Proximity voice is [Voice](voice.md).

`code/client/CMakeLists.txt` is the wall. `VANTAMP_CLIENT_SDK_FILES` is the
only list that gets plugin-sdk include directories. A file not on that list
cannot name a `CPed` even by accident. Do not widen it to make a call site
shorter — put the call behind an SDK function.

plugin-sdk is **header-only**. Its `.cpp` bodies are 1.0 US addresses and are
not linked. Every game function is a signature in `sdk/patterns.*`, called
through a function pointer or a vtable slot. See
[SDK and patterns](sdk-and-patterns.md).

## Who owns what

The framework's `ownerGUID` is the authority bit.

| Thing | Owner | Everyone else |
|-------|-------|----------------|
| A player | That player's client | Relays the snapshot; applies it to a remote ped |
| A vehicle | The last driver (then the server if they disconnect) | Interpolates pose, holds doors, swaps the pad |
| Enter / exit / jack | The client that started the walk | Plays the same task after a reliable RPC |
| Door swing on an empty car | Whoever is climbing, via `VehicleDamageReport` | `OpenDoor` only when the ratio actually moved |
| Door swing on a driven car | The driver, on the replica | Held every frame; passengers do not report |

A seated player without their car is meaningless. `PlayerEntity` declares the
vehicle as an interest dependency so the grid pulls the car in with them,
uncounted.

## Three kinds of wire

Not everything is a replica field.

1. **Unreliable snapshot** (`PlayerEntity`, `VehicleEntity`). Position, pad,
   health, colours, packed damage. Append-only. A late joiner must see a wreck
   and a nickname without waiting for a change that already happened.
2. **Reliable RPC**. Enter, exit, shot, reload, death, bystander damage. These
   are transitions that play once. As a flag they restart; as an unreliable
   packet they can be missed.
3. **Nothing.** Some GTA flags are outputs (`bIsDucking`, `bIsStanding`,
   `m_fSteerAngle`). Writing them does nothing, or fights the task that owns
   them. Run the task or swap the pad instead.

Restart the server whenever a shared entity field is added. The serializer is
positional. A mismatched pair deserializes garbage silently.

## Scripting

Resources talk to C++ through `code/server/src/scripting/bindings.cpp`.
`Vehicles.create` and `Player.spawn` are the ones freeroam uses. If a resource
listens for `playerReady` or `playerDeath`, C++ does not also spawn — that is
the resource's job.

The current freeroam spawn is the Los Santos waterfront so two clients start
near water and a test Sultan. Grove Street is commented next to the numbers
in `shared/spawn_points.h` and should go back when the waterfront has been
watched for long enough.
