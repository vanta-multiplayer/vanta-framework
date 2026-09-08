# Player sync

Remote players are driven by **input replay**, not by animation replication.

Each remote player is a real `CPlayerPed` in its own slot. The owning client
sends controller state; the receiving client writes that into the slot's pad
and lets GTA's own `ProcessControl` walk the ped. Position sync only corrects
where that walk has drifted.

This is the same idea as MTA's "pure sync". The numbers in
`shared/sync_rates.h` came from there: 100 ms nearby, 1500 ms when distant,
input lease of `max(500, 3 × interval)`.

## Apply order

`game/entities/remote_players.cpp` swaps the game's single-player globals
(pad, `PlayerInFocus`, camera rotation) around each remote ped's
`ProcessControl`, then corrects **after** that call and **outside** the swap.
Inside the swap a fault takes the process down with no exception to report.

`game/sync/player_sync.cpp` is the send / apply loop. Occupancy is checked
first: a ped who is seated, walking to a door or climbing out is left alone
by the on-foot interpolator.

## What rides the snapshot

| Field | Why it is not derived |
|-------|------------------------|
| Stick + buttons | The game consumes input, not animation names |
| Accelerator / brake | Analogue, and both can be held (a drift). A bit cannot express that |
| `cameraRotation` | Stick forward means "away from the camera". Process a remote ped with the local camera and they walk sideways |
| `aimHorizontal` / `aimVertical` | The hands. Not the same as the camera — the camera is 10 Hz and the IK would step in tenths |
| `isDucked` | The crouch button is already in the mask, but GTA can refuse the transition. State, not edge |
| `isInWater` | The in-water **task** is running. `bSubmergedInWater` is false on the surface |
| `isInAir` | Sent to **gate velocity**, not written onto the ped. The game recomputes it |
| Weapon + ammo + `isReloading` | A held weapon is a model. Reloading is a task — writing ammo during the anim is MTA's #4503 |
| `vehicleNetworkId` / `vehicleSeat` | Late joiners warp into the seat. Live enter/exit is an RPC |
| `interior` | GTA area code. The owner reads it off the ped. Script sets send `WorldSpace` so `currArea` changes too |
| `dimension` | Virtual world. Server-authoritative; the owner echoes, it does not invent |
| `money` / `frozen` / `collisions` | Script-owned. The owner echoes and never reads GTA |
| `wantedLevel` | Stars. The owner reports what GTA has; script set sends `PlayerScript` first |
| `fightingStyle` | Fists. The owner reports GTA's style (4 is standard); 0 means remotes leave it |
| `oxygen` | Breath. The owner reports `CPlayerData::m_fBreath`; script set sends `PlayerScript` first |
| `walkingStyle` | `CPed::m_nAnimGroup`. The owner reports it; script set sends `PlayerScript` then `SetMoveAnim` |
| `alpha` | Clump alpha 0–255. Script-owned. Applied every frame — GTA rewrites it from ProcessControl |
| `onFire` | `m_pFire`. The owner reports it; script set starts or extinguishes |
| `maxHealth` | Script-owned. The owner echoes; script set sends `PlayerScript` first |
| `isDrowning` | `bIsDrowning`. The owner reports underwater damage |
| `hasJetPack` / `jetpack` | Script-owned. Installs `TASK_SIMPLE_JETPACK` on the primary slot (MTA `SetHasJetPack`) |

Position, velocity and rotation live on `NetworkEntity`, not on `PlayerEntity`.
Re-declaring them would put the same numbers on the wire twice.

Combat events (shot, reload, death) are their own reliable packets. See
`shared/rpc/player_shot.h`, `player_reload.h`, `player_death.h`.

## Interpolation

- **Position** uses the framework interpolator. Error is paid off across the
  send interval. Re-aim every `kPlayerMs`, not only on change — otherwise the
  residue when someone stops walking stays forever.
- **Heading** is a plain lerp with wrap (`OffsetRadians`), aimed on arrival,
  turned frame by frame. Read and write `m_fCurrentRotation` (+0x558), not
  the matrix. The matrix is a quarter turn away; using it drew every remote
  player 90° off, permanently.
- **Warp** when the gap exceeds `(5 + 5 × speed) × tick/100`. A sprinter
  covers more ground between packets than a walker.
- **Velocity** is gated the MTA way: every update in the air, every fourth on
  the ground, and only on an update. A ped on the ground already gets move
  speed from the walk animation. Writing the owner's reading on top is
  movement counted twice. A swimmer is not airborne.

## Pad edges

`WriteRemoteInput` must keep `OldState` as the **previous** frame. GTA's
discrete actions (`JumpJustDown` and friends) are `NewState && !OldState`.
If old and new are copied from the same packet, nobody knows how to jump,
crouch or enter a car, and it looks like an animation bug.

Buttons snap (edges). Analogue axes lerp across `kPlayerMs` so a remote
driver does not stutter when they change direction.

## Things that look syncable and are not

- `bIsDucking` is an output. Crouch is `CTaskSimpleDuck` on
  `TASK_SECONDARY_DUCK`, ended with `MakeAbortable`.
- The crouch **button** is dropped from the replayed pad. Two mechanisms on
  one pose flicker.
- Jump is the controller button. Do not port a one-shot jump task event —
  that was a workaround for the `OldState` bug.
- Camera scripting (`iCamSync`) is not implemented on purpose. It never
  reached other clients in MTA either. The rotation that matters already
  rides the per-tick path.
