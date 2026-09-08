// Freeroam client — chat slash-commands, key shortcuts, and server notifies.
// Reserved: resourceStart/Stop, chatSend (veto), chatMessage, voiceStart/Stop.

function sendCommand(name, args = []) {
    Events.emitServer('freeroam:command', { name, args: args.map(String) });
}

function parseSlash(line) {
    const trimmed = String(line || '').trim();
    if (!trimmed.startsWith('/')) {
        return null;
    }
    const body = trimmed.slice(1).trim();
    if (!body) {
        return null;
    }
    const parts = body.split(/\s+/);
    return { name: parts[0].toLowerCase(), args: parts.slice(1) };
}

Events.on('resourceStart', (resourceName) => {
    if (resourceName && resourceName !== 'freeroam') {
        return;
    }
    console.log('freeroam: client scripts running');
    if (typeof Voice !== 'undefined' && Voice.hasMicrophone && !Voice.hasMicrophone()) {
        console.log('freeroam: no microphone — listen-only voice');
    }
});

Events.on('resourceStop', (resourceName) => {
    if (resourceName && resourceName !== 'freeroam') {
        return;
    }
    console.log('freeroam: client stopped');
});

// Intercept slash commands before they hit the server as plain chat.
Events.on('chatSend', (text) => {
    const parsed = parseSlash(text);
    if (!parsed) {
        return; // allow normal chat
    }
    sendCommand(parsed.name, parsed.args);
    return false; // veto — do not send "/veh" as a chat line
});

Events.on('chatMessage', (message) => {
    // Default overlay already shows it; log for debugging.
    if (!message) {
        return;
    }
    const author = message.author || 'chat';
    const text = message.text || '';
    console.log(`freeroam chat: [${author}] ${text}`);
});

Events.on('freeroam:notify', (payload) => {
    const text = payload && payload.text ? payload.text : String(payload || '');
    console.log(`freeroam: ${text}`);
});

Events.on('freeroam:playerJoined', (payload) => {
    if (!payload) {
        return;
    }
    console.log(`freeroam: ${payload.name} (#${payload.id}) is in freeroam`);
});

Events.on('voiceStart', () => {
    console.log('freeroam: you started talking');
});

Events.on('voiceStop', () => {
    console.log('freeroam: you stopped talking');
});

// Shortcuts (only fire when the game has focus and chat UI is closed).
Key.bind('f4', 'down', () => {
    sendCommand('veh', ['560']);
});

Key.bind('f5', 'down', () => {
    sendCommand('weapons');
});

Key.bind('f6', 'down', () => {
    sendCommand('heal');
});

// Optional: print local replica stats.
Key.bind('f7', 'down', () => {
    const me = typeof localPlayer !== 'undefined' ? localPlayer : Player.getLocal();
    if (!me) {
        console.log('freeroam: localPlayer not ready');
        return;
    }
    const p = me.position;
    console.log(
        `freeroam: ${me.nickname} hp=${me.health} arm=${me.armour} ` +
            `pos=${p.x.toFixed(1)},${p.y.toFixed(1)},${p.z.toFixed(1)} ` +
            `veh=${me.inVehicle} weapon=${me.weapon}`,
    );
});
