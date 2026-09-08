// Freeroam server — owns spawn, world defaults, and gameplay commands.
// Native Events: resourceStart/Stop, playerReady/Death, vehicle*, playerVoice*, consoleCommand.
// Client commands arrive on Events.onClient (Framework wraps the player — re-wrap as Vanta Player).

const COLOR = {
    system: Color.fromRGB(100, 200, 255),
    success: Color.fromRGB(80, 220, 120),
    warn: Color.fromRGB(255, 180, 60),
    death: Color.fromRGB(255, 90, 90),
    voice: Color.fromRGB(180, 140, 255),
};

/** @type {Map<number, number>} playerId -> personal vehicle network id */
const personalVehicles = new Map();

/** Locked demo car — vehicleStartEnter returns false while locked. */
let lockedDemoCarId = 0;

function asPlayer(handle) {
    if (!handle) {
        return null;
    }
    if (typeof handle.giveWeapon === 'function' && typeof handle.spawn === 'function') {
        return handle;
    }
    return new Player(handle.id);
}

function announce(text, color = COLOR.system) {
    Chat.sendToAll(text, { author: 'Freeroam', color });
}

function tell(player, text, color = COLOR.system) {
    Chat.sendToPlayer(player, text, { author: 'Freeroam', color });
}

function notifyClient(player, text) {
    player.emit('freeroam:notify', JSON.stringify({ text }));
}

function broadcastClient(eventName, payload) {
    Events.emitAllClients(eventName, payload);
}

function spawnPlayer(player, reason) {
    const { x, y, z, heading } = Freeroam.spawn;
    // Do not pass `undefined` for optional model — v8pp from_v8<uint32_t>
    // throws a C++ exception (0xE06D7363) and takes down the process.
    player.spawn(x, y, z, heading, 0, 100, 0);
    player.health = 100;
    player.armour = 0;
    player.money = 5000;
    player.wantedLevel = 0;
    player.takeAllWeapons();
    player.giveWeapon(Freeroam.weapons.pistol, 60);
    player.giveWeapon(Freeroam.weapons.knife, 1);
    console.log(`freeroam: spawn ${player.nickname} (${player.id}) [${reason}]`);
}

function giveStarterWeapons(player) {
    player.giveWeapon(Freeroam.weapons.pistol, 120);
    player.giveWeapon(Freeroam.weapons.deagle, 60);
    player.giveWeapon(Freeroam.weapons.shotgun, 40);
    player.giveWeapon(Freeroam.weapons.m4, 200);
    player.giveWeapon(Freeroam.weapons.grenade, 5);
}

function destroyPersonalVehicle(playerId) {
    const id = personalVehicles.get(playerId);
    if (id == null) {
        return;
    }
    personalVehicles.delete(playerId);
    try {
        Vehicles.destroy(id);
    } catch (err) {
        console.log(`freeroam: destroy vehicle ${id} failed: ${err}`);
    }
}

function spawnVehicleFor(player, model) {
    const mid = Number(model) || 560;
    const pos = player.position;
    const x = pos.x + 3.0;
    const y = pos.y;
    const z = pos.z;

    destroyPersonalVehicle(player.id);

    const car = Vehicles.create(mid, x, y, z, 0);
    if (!car) {
        tell(player, `Could not create vehicle model ${mid}.`, COLOR.warn);
        return null;
    }

    car.setColor(Color.fromRGB(30, 144, 255), Color.fromRGB(20, 20, 20));
    car.plateText = 'FREE';
    car.engine = true;
    personalVehicles.set(player.id, car.id);
    tell(player, `Spawned ${car.name || mid} (#${car.id}).`, COLOR.success);
    return car;
}

function applySkin(player, raw) {
    const key = String(raw || '').toLowerCase();
    const fromName = Freeroam.skins[key];
    const model = fromName !== undefined ? fromName : Number(raw);
    if (!Number.isFinite(model)) {
        tell(player, 'Usage: /skin cj|truth|sweet|smoke|ryder|ballas|<id>', COLOR.warn);
        return;
    }
    player.model = model;
    tell(player, `Skin set to ${model}.`, COLOR.success);
}

function giveNamedGun(player, name) {
    const key = String(name || 'pistol').toLowerCase();
    const weapon = Freeroam.weapons[key];
    if (weapon == null) {
        tell(player, `Unknown gun. Try: ${Object.keys(Freeroam.weapons).join(', ')}`, COLOR.warn);
        return;
    }
    player.giveWeapon(weapon, 200);
    tell(player, `Gave ${key}.`, COLOR.success);
}

function handleCommand(player, name, args) {
    const cmd = String(name || '').toLowerCase();

    switch (cmd) {
        case 'help':
        case 'commands':
            for (const line of Freeroam.help) {
                tell(player, line);
            }
            break;

        case 'veh':
        case 'car':
        case 'vehicle':
            spawnVehicleFor(player, args[0]);
            break;

        case 'gun':
        case 'weapon':
            giveNamedGun(player, args[0]);
            break;

        case 'weapons':
        case 'kit':
            giveStarterWeapons(player);
            tell(player, 'Starter weapons equipped.', COLOR.success);
            break;

        case 'heal':
        case 'hp':
            player.health = player.maxHealth || 100;
            player.armour = 100;
            tell(player, 'Healed.', COLOR.success);
            break;

        case 'repair':
        case 'fix': {
            const vehicle = player.getOccupiedVehicle();
            if (!vehicle) {
                tell(player, 'You are not in a vehicle.', COLOR.warn);
                break;
            }
            vehicle.fix();
            vehicle.health = 1000;
            vehicle.engineBroken = false;
            tell(player, 'Vehicle repaired.', COLOR.success);
            break;
        }

        case 'skin':
            applySkin(player, args[0]);
            break;

        case 'pos':
        case 'position': {
            const p = player.position;
            const msg = `pos ${p.x.toFixed(2)} ${p.y.toFixed(2)} ${p.z.toFixed(2)}  interior=${player.interior} dim=${player.dimension}`;
            tell(player, msg);
            console.log(`freeroam: ${player.nickname} ${msg}`);
            break;
        }

        case 'tp':
        case 'teleport': {
            const x = Number(args[0]);
            const y = Number(args[1]);
            const z = Number(args[2]);
            if (![x, y, z].every(Number.isFinite)) {
                tell(player, 'Usage: /tp x y z', COLOR.warn);
                break;
            }
            player.position = new Vector3(x, y, z);
            tell(player, `Teleported to ${x} ${y} ${z}.`, COLOR.success);
            break;
        }

        case 'jetpack':
            player.jetpack = !player.jetpack;
            tell(player, `Jetpack ${player.jetpack ? 'on' : 'off'}.`, COLOR.success);
            break;

        case 'weather': {
            const id = Number(args[0]);
            if (!Number.isFinite(id)) {
                tell(player, `Weather is ${World.getWeather()}. Usage: /weather 0-45`, COLOR.warn);
                break;
            }
            World.setWeather(id);
            announce(`Weather → ${id} (by ${player.nickname})`);
            break;
        }

        case 'time': {
            const h = Number(args[0]);
            const m = Number(args[1] ?? 0);
            if (!Number.isFinite(h)) {
                tell(player, `Time is ${World.getHour()}:${String(World.getMinute()).padStart(2, '0')}. Usage: /time h m`, COLOR.warn);
                break;
            }
            World.setTime(h, m);
            announce(`Time → ${World.getHour()}:${String(World.getMinute()).padStart(2, '0')} (by ${player.nickname})`);
            break;
        }

        default:
            tell(player, `Unknown command /${cmd}. Type /help.`, COLOR.warn);
            break;
    }
}

// --- Native / reserved server events -----------------------------------------

Events.on('resourceStart', (resourceName) => {
    if (resourceName && resourceName !== 'freeroam') {
        return;
    }

    World.setTime(12, 0);
    World.setMinuteDuration(60000);
    World.setWeather(1);
    World.setGravity(0.008);
    Voice.setRange(40);

    for (const slot of Freeroam.dock) {
        const car = Vehicles.create(slot.model, slot.x, slot.y, slot.z, slot.heading);
        if (!car) {
            console.log(`freeroam: failed to create dock ${slot.name}`);
            continue;
        }
        car.setColor(Color.fromRGB(20, 200, 40), Color.fromRGB(8, 8, 8));
        car.plateText = slot.name.slice(0, 8).toUpperCase();
        car.engine = true;
        console.log(`freeroam: dock ${slot.name} #${car.id}`);
    }

    // Demo locked car a bit inland — enter is vetoed until unlocked via console.
    const locked = Vehicles.create(560, 2888.0, -1660.0, 10.9, 0);
    if (locked) {
        locked.locked = true;
        locked.plateText = 'LOCKED';
        locked.setColor(Color.fromRGB(200, 40, 40), Color.fromRGB(10, 10, 10));
        lockedDemoCarId = locked.id;
        console.log(`freeroam: locked demo car #${lockedDemoCarId} (console: unlock)`);
    }

    announce('Freeroam loaded. Type /help — F4/F5/F6 on client.');
    console.log('freeroam: resourceStart complete');
});

Events.on('resourceStop', (resourceName) => {
    if (resourceName && resourceName !== 'freeroam') {
        return;
    }
    personalVehicles.clear();
    console.log('freeroam: resourceStop');
});

Events.on('playerReady', (player) => {
    spawnPlayer(player, 'ready');
    tell(player, `Welcome, ${player.nickname}. /help for commands.`, COLOR.success);
    notifyClient(player, 'Welcome to freeroam. Press T for chat, F4/F5/F6 for shortcuts.');
    announce(`${player.nickname} joined.`, COLOR.success);
    broadcastClient('freeroam:playerJoined', { id: player.id, name: player.nickname });
});

Events.on('playerDeath', (player, killerId, weapon) => {
    const killer =
        killerId && killerId !== 0 && killerId !== player.id ? new Player(killerId) : null;
    const how = killer ? `killed by ${killer.nickname} (weapon ${weapon})` : `died (weapon ${weapon})`;
    announce(`${player.nickname} ${how}.`, COLOR.death);

    setTimeout(() => {
        try {
            spawnPlayer(player, 'death');
            tell(player, 'Respawned.', COLOR.success);
        } catch (err) {
            console.log(`freeroam: respawn failed for ${player.id}: ${err}`);
        }
    }, 1500);
});

Events.on('vehicleStartEnter', (player, vehicle, seat, jacked, door) => {
    console.log(
        `freeroam: ${player.nickname} startEnter veh=${vehicle.id} seat=${seat} jacked=${jacked} door=${door}`,
    );
    if (vehicle.locked || vehicle.id === lockedDemoCarId) {
        tell(player, 'That vehicle is locked.', COLOR.warn);
        return false;
    }
});

Events.on('vehicleEnter', (player, vehicle, seat) => {
    console.log(`freeroam: ${player.nickname} entered veh=${vehicle.id} seat=${seat}`);
    if (seat === 0) {
        vehicle.engine = true;
    }
});

Events.on('vehicleStartExit', (player, vehicle, seat, door) => {
    console.log(`freeroam: ${player.nickname} startExit veh=${vehicle.id} seat=${seat} door=${door}`);
});

Events.on('vehicleExit', (player, vehicle, seat) => {
    console.log(`freeroam: ${player.nickname} exited veh=${vehicle.id} seat=${seat}`);
});

Events.on('playerVoiceStart', (raw) => {
    const player = asPlayer(raw);
    if (!player) {
        return;
    }
    console.log(`freeroam: voice start ${player.nickname}`);
});

Events.on('playerVoiceStop', (raw) => {
    const player = asPlayer(raw);
    if (!player) {
        return;
    }
    console.log(`freeroam: voice stop ${player.nickname}`);
});

// Server console (not in-game chat): weather 3 | time 22 30 | unlock | announce hi
Events.on('consoleCommand', (command, args) => {
    const cmd = String(command || '').toLowerCase();
    const list = Array.isArray(args) ? args.map(String) : [];

    switch (cmd) {
        case 'weather':
            World.setWeather(Number(list[0]) || 0);
            announce(`Console set weather to ${World.getWeather()}.`);
            break;
        case 'time':
            World.setTime(Number(list[0]) || 0, Number(list[1]) || 0);
            announce(`Console set time to ${World.getHour()}:${String(World.getMinute()).padStart(2, '0')}.`);
            break;
        case 'unlock':
            if (lockedDemoCarId) {
                const car = new Vehicle(lockedDemoCarId);
                car.locked = false;
                lockedDemoCarId = 0;
                announce('Demo car unlocked.', COLOR.success);
            }
            break;
        case 'announce':
        case 'say':
            announce(list.join(' ') || '(empty)');
            break;
        case 'freeroam':
            console.log('freeroam console: weather <id> | time <h> <m> | unlock | announce <text>');
            break;
        default:
            break;
    }
});

// Client → server commands (chat /keys). Re-wrap Framework player → Vanta Player.
Events.onClient('freeroam:command', (rawPlayer, payload) => {
    const player = asPlayer(rawPlayer);
    if (!player) {
        return;
    }

    let data = payload;
    if (typeof payload === 'string') {
        try {
            data = JSON.parse(payload);
        } catch {
            data = { name: payload, args: [] };
        }
    }

    const name = data && data.name != null ? data.name : '';
    const args = data && Array.isArray(data.args) ? data.args : [];
    handleCommand(player, name, args);
});

Events.onLocal('freeroam:booted', () => {
    console.log('freeroam: local bus ready');
});
Events.emitLocal('freeroam:booted');
