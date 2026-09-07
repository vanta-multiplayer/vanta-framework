export type CommandSpec = {
    readonly name: string;
    readonly aliases?: readonly string[];
    readonly usage: string;
    readonly hint: string;
    readonly args?: readonly string[];
};

export type Suggestion = {
    readonly label: string;
    readonly hint: string;
    readonly fill: string;
};

export const CLIENT_COMMANDS: readonly CommandSpec[] = [
    {name: "help", aliases: ["?"], usage: "help", hint: "List client commands"},
    {name: "clear", aliases: ["cls"], usage: "clear", hint: "Clear the console"},
    {name: "connect", usage: "connect <host[:port]>", hint: "Join a server"},
    {name: "disconnect", aliases: ["dc"], usage: "disconnect", hint: "Leave the current server"},
    {name: "reconnect", usage: "reconnect", hint: "Join the last server again"},
    {name: "quit", aliases: ["exit", "q"], usage: "quit", hint: "Close the game"},
    {name: "name", aliases: ["nick"], usage: "name <nickname>", hint: "Set your player name"},
    {name: "netgraph", aliases: ["shownetgraph"], usage: "netgraph [on|off]", hint: "Toggle the netgraph (saved)", args: ["on", "off"]},
    {name: "status", usage: "status", hint: "Show connection and client info"},
    {name: "version", usage: "version", hint: "Print the client version"},
];

export function findCommand(verb: string): CommandSpec | undefined {
    const name = verb.toLowerCase();

    return CLIENT_COMMANDS.find(
        (command) => command.name === name || command.aliases?.includes(name),
    );
}

export function asCommandCatalog(value: unknown): CommandSpec[] {
    if (!Array.isArray(value)) {
        return [...CLIENT_COMMANDS];
    }

    const list: CommandSpec[] = [];

    for (const item of value) {
        if (!item || typeof item !== "object") {
            continue;
        }

        const data = item as Record<string, unknown>;
        const name = typeof data.name === "string" ? data.name.trim() : "";

        if (name === "") {
            continue;
        }

        list.push({
            name,
            aliases: Array.isArray(data.aliases) ? data.aliases.map((alias) => String(alias)) : undefined,
            usage: typeof data.usage === "string" ? data.usage : name,
            hint: typeof data.hint === "string" ? data.hint : "",
            args: Array.isArray(data.args) ? data.args.map((arg) => String(arg)) : undefined,
        });
    }

    return list.length > 0 ? list : [...CLIENT_COMMANDS];
}

export function commandSuggestions(
    input: string,
    extras?: {recent?: readonly string[]; catalog?: readonly CommandSpec[]},
): Suggestion[] {
    const leading = input.trimStart();

    if (leading === "") {
        return [];
    }

    const catalog = extras?.catalog ?? CLIENT_COMMANDS;
    const trailingSpace = input.endsWith(" ") && leading.length > 0;
    const parts = leading.split(/\s+/);
    const verb = (parts[0] ?? "").toLowerCase();

    if (parts.length === 1 && !trailingSpace) {
        return catalog.filter(
            (command) =>
                command.name.startsWith(verb) || command.aliases?.some((alias) => alias.startsWith(verb)),
        ).map((command) => ({
            label: command.name,
            hint: command.hint,
            fill: command.args || command.name === "connect" || command.name === "name" ? `${command.name} ` : command.name,
        }));
    }

    const spec = catalog.find((command) => command.name === verb || command.aliases?.includes(verb));

    if (!spec) {
        return [];
    }

    if (spec.name === "connect") {
        const host = trailingSpace ? "" : (parts[1] ?? "").toLowerCase();

        return (extras?.recent ?? [])
            .filter((address) => host === "" || address.toLowerCase().includes(host))
            .slice(0, 8)
            .map((address) => ({
                label: address,
                hint: "Recent",
                fill: `connect ${address}`,
            }));
    }

    if (spec.args) {
        const token = trailingSpace ? "" : (parts[1] ?? "").toLowerCase();

        return spec.args
            .filter((arg) => token === "" || arg.startsWith(token))
            .map((arg) => ({
                label: arg,
                hint: spec.hint,
                fill: `${spec.name} ${arg}`,
            }));
    }

    return [];
}
