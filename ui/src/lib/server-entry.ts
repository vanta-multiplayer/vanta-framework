import type {ServerEntry} from "../state/bridge";

function text(value: unknown): string {
    return typeof value === "string" ? value.trim() : "";
}

function count(value: unknown): number {
    return typeof value === "number" && Number.isFinite(value) ? value : 0;
}

function tags(value: unknown): string[] | undefined {
    if (Array.isArray(value)) {
        const list = value.map((item) => String(item).trim()).filter(Boolean);
        return list.length > 0 ? list : undefined;
    }

    if (typeof value === "string") {
        const list = value.split(",").map((item) => item.trim()).filter(Boolean);
        return list.length > 0 ? list : undefined;
    }

    return undefined;
}

export function asServerEntry(value: unknown): ServerEntry | null {
    if (typeof value === "string") {
        const address = value.trim();

        if (address === "") {
            return null;
        }

        return {name: address, address, players: 0, maxPlayers: 0, ping: 0};
    }

    if (value === null || typeof value !== "object") {
        return null;
    }

    const data = value as Record<string, unknown>;
    const host = text(data.host);
    const port = count(data.port);
    const address = text(data.address) || (host !== "" ? (port > 0 ? `${host}:${port}` : host) : "");
    const name = text(data.name) || address;

    if (address === "" && name === "") {
        return null;
    }

    return {
        id: text(data.id) || undefined,
        name,
        address: address || name,
        players: count(data.players),
        maxPlayers: count(data.maxPlayers),
        ping: count(data.ping),
        language: text(data.language) || undefined,
        gamemode: text(data.gamemode) || undefined,
        website: text(data.website) || undefined,
        description: text(data.description) || undefined,
        tags: tags(data.tags),
        banner: text(data.banner) || undefined,
        avatar: text(data.avatar) || undefined,
    };
}
