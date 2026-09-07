import type {ServerEntry} from "../state/bridge";

const palettes = [
    {from: "#2a140a", to: "#f28b3a"},
    {from: "#1a100c", to: "#b94d12"},
    {from: "#24180e", to: "#ffb45e"},
    {from: "#17130f", to: "#e56b25"},
    {from: "#20140c", to: "#d86838"},
] as const;

function hashSeed(value: string): number {
    let hash = 2166136261;

    for (let index = 0; index < value.length; index += 1) {
        hash ^= value.charCodeAt(index);
        hash = Math.imul(hash, 16777619);
    }

    return hash >>> 0;
}

export function serverSeed(server: ServerEntry): string {
    return server.address || server.name || "vanta";
}

export function serverLetter(server: ServerEntry): string {
    const name = server.name.trim();
    return (name[0] || "?").toUpperCase();
}

export function serverPalette(server: ServerEntry): {from: string; to: string} {
    return palettes[hashSeed(serverSeed(server)) % palettes.length];
}
