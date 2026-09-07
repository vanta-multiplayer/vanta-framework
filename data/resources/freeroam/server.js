// Waterfront test spawn - same numbers as shared/world/spawn_points.h.
const SPAWN = { x: 2899.41, y: -1672.64, z: 12.89 };

// On the dock, not +4 off the pier into the water.
const SULTAN = { x: 2865.93, y: -1665.88, z: 10.88 };

Events.on('resourceStart', () => {
    const car = Vehicles.create(560, SULTAN.x, SULTAN.y, SULTAN.z, 0);
    if (!car) {
        console.log('freeroam: could not create the test Sultan');
        return;
    }

    car.setColor(Color.fromRGB(20, 200, 40), Color.fromRGB(8, 8, 8));
    console.log(`freeroam: test Sultan ${car.id} at ${SULTAN.x}, ${SULTAN.y}, ${SULTAN.z}`);
});

Events.on('playerReady', (player) => {
    console.log(`freeroam: spawning ${player.nickname} (${player.id})`);
    player.spawn(SPAWN.x, SPAWN.y, SPAWN.z);
});

Events.on('playerDeath', (player, killerId, weapon) => {
    console.log(`freeroam: ${player.nickname} died (killer ${killerId} weapon ${weapon})`);
    player.spawn(SPAWN.x, SPAWN.y, SPAWN.z);
});
