// Waterfront test spawn - same numbers as shared/spawn_points.h.
const SPAWN = { x: 2899.41, y: -1672.64, z: 12.89 };

// On the dock, not +4 off the pier into the water.
const SULTAN = { x: 2865.93, y: -1665.88, z: 10.88 };

Events.on('resourceStart', () => {
    const id = Vehicles.create(560, SULTAN.x, SULTAN.y, SULTAN.z, 0);
    console.log(`freeroam: test Sultan ${id} at ${SULTAN.x}, ${SULTAN.y}, ${SULTAN.z}`);
});

Events.on('playerReady', (player) => {
    console.log(`freeroam: spawning ${player.nickname} (${player.id})`);
    player.spawn(SPAWN.x, SPAWN.y, SPAWN.z);
});

Events.on('playerDeath', (player, killerId, weapon) => {
    console.log(`freeroam: ${player.nickname} died (killer ${killerId} weapon ${weapon})`);
    player.spawn(SPAWN.x, SPAWN.y, SPAWN.z);
});
