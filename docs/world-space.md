# Interiors and dimensions

MTA's pair, on the snapshot.

**Interior** is GTA's area code (`CEntity::m_nAreaCode`, 0 = outside). Walking
through an enex writes it on the ped; the owner reports it. Script
`player.interior = n` also writes `CGame::currArea` and calls
`CStreaming::RemoveBuildingsNotInArea`, which is what actually loads the
shop and unloads the street.

**Dimension** is not a GTA concept. It is a virtual world index
(`setElementDimension`). The interest grid is position-only, so isolation
is applied on the client: a replica in another dimension is not given a
ped or a car. Nametags and voice require the same dimension *and* the
same interior.

Restart the server after this field append. The serializer is positional.

## Wire

Both `PlayerEntity` and `VehicleEntity` append `interior` (u8) and
`dimension` (u16). Owner puresync reports interior from GTA and **echoes**
dimension — it never invents one. A server write of dimension would be
stomped on the next owner snapshot unless the owning client copies it
first. That copy is the reliable `WorldSpace` RPC.

`LocalPlayerSpawn` carries the same pair so a spawn into an interior
applies `currArea` before the first snapshot.

## Script

```
player.interior                 // get / set, 0–255
player.dimension                // get / set, 0–65535
player.spawn(x, y, z, heading?, model?, health?, armour?, interior?, dimension?)

Vehicles.create(model, x, y, z, heading?, primary?, secondary?, interior?, dimension?)
Vehicles.setInterior(id, n)
Vehicles.setDimension(id, n)
Vehicles.getInterior(id)
Vehicles.getDimension(id)
```

An empty car is server-owned, so a script write persists until a driver
claims it. The driver then echoes dimension and reports interior from the
GTA vehicle. Setting a driven car sends `WorldSpace` to that owner.

## What you will see

Two clients in dimension 0 see each other. `player.dimension = 1` on one
of them: the other loses the ped, the nametag and the voice. Set both
back to 0 and they reappear. Scripting someone into interior 5 loads that
area on their client; a player still outside is gone from their screen.
