import type {JSX} from "solid-js";
import {For, Show, createSignal} from "solid-js";
import {emit} from "../../state/bridge";
import {connectTo, servers} from "../../state/session";
import RefreshCw from "lucide-solid/icons/refresh-cw";

export function Servers(): JSX.Element {
    const [query, setQuery] = createSignal("");

    const filtered = () => {
        const q = query().trim().toLowerCase();
        const list = servers();

        if (q === "") {
            return list;
        }

        return list.filter(
            (server) =>
                server.name.toLowerCase().includes(q) || server.address.toLowerCase().includes(q),
        );
    };

    return (
        <div class="flex h-full min-h-0">
            <aside class="flex w-[length:var(--servers-rail)] shrink-0 flex-col border-r border-border">
                <div class="flex items-center justify-between border-b border-border px-[length:var(--space-xl)] py-[length:var(--space-xl)]">
                    <h1>Servers</h1>
                    <button
                        type="button"
                        title="Reload"
                        class="grid size-[length:var(--nav-icon)] place-items-center text-muted hover:text-foreground"
                        onClick={() => emit("servers:reload")}
                    >
                        <RefreshCw class="size-6" />
                    </button>
                </div>
                <div class="flex-1 border-b border-border px-[length:var(--space-xl)] py-[length:var(--space-xl)]">
                    <input
                        class="text-body h-[length:var(--control-height)] w-full rounded-lg border border-border
                               bg-brown-dark px-3.5 outline-none select-text placeholder:text-muted/50
                               focus:border-primary"
                        type="search"
                        placeholder="Filter"
                        value={query()}
                        onInput={(event) => setQuery(event.currentTarget.value)}
                    />
                </div>
                <p class="mt-auto px-[length:var(--space-xl)] py-8 text-[length:var(--text-label)] text-muted">
                    {filtered().length} servers
                </p>
            </aside>
            <div class="min-w-0 flex-1 overflow-y-auto">
                <Show
                    when={filtered().length > 0}
                    fallback={<p class="px-[length:var(--space-xl)] pt-12 text-muted">No servers yet. Type an address from Home.</p>}
                >
                    <table class="w-full border-collapse text-left">
                        <thead class="sticky top-0 bg-background text-[length:var(--text-label)] text-muted">
                            <tr>
                                <th class="px-[length:var(--space-xl)] py-4 font-medium">Name</th>
                                <th class="px-4 py-4 font-medium">Players</th>
                                <th class="px-[length:var(--space-xl)] py-4 font-medium">Ping</th>
                            </tr>
                        </thead>
                        <tbody>
                            <For each={filtered()}>
                                {(server) => (
                                    <tr
                                        class="cursor-pointer border-t border-border hover:bg-surface"
                                        onClick={() => connectTo(server.address)}
                                    >
                                        <td class="px-[length:var(--space-xl)] py-3.5">
                                            <div class="truncate">{server.name}</div>
                                            <div class="text-[length:var(--text-label)] text-muted">
                                                {server.address}
                                            </div>
                                        </td>
                                        <td class="px-4 py-3.5 tabular-nums text-muted">
                                            {server.players}/{server.maxPlayers}
                                        </td>
                                        <td class="px-[length:var(--space-xl)] py-3.5 tabular-nums text-muted">
                                            {server.ping} ms
                                        </td>
                                    </tr>
                                )}
                            </For>
                        </tbody>
                    </table>
                </Show>
            </div>
        </div>
    );
}
