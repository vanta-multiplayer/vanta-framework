// Waterfront test spawn - same numbers as shared/spawn_points.h.
const SPAWN = { x: 2899.41, y: -1672.64, z: 12.89 };

Events.on('playerReady', (player) => {
    console.log(`freeroam: spawning ${player.nickname} (${player.id})`);
    player.spawn(SPAWN.x, SPAWN.y, SPAWN.z);
});

Events.on('playerDeath', (player, killerId, weapon) => {
    console.log(`freeroam: ${player.nickname} died (killer ${killerId} weapon ${weapon})`);
    player.spawn(SPAWN.x, SPAWN.y, SPAWN.z);
});
