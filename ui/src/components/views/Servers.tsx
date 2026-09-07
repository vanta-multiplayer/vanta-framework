import type {JSX} from "solid-js";
import {For, Show, createSignal} from "solid-js";
import {emit} from "../../state/bridge";
import {openJoin, servers} from "../../state/session";
import {Input} from "../ui/input";
import {ServerAvatar} from "../ServerIdentity";
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
                <div class="flex items-center justify-between border-b border-border px-[length:var(--space-xl)] py-8">
                    <h1 class="text-2xl">Servers</h1>
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
                    <Input
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
                    fallback={<p class="px-[length:var(--space-xl)] pt-12 text-muted">No servers yet. Use Direct connect if you have an address.</p>}
                >
                    <table class="w-full border-collapse text-left text-sm">
                        <thead class="sticky top-0 bg-background/90 text-[11px] font-bold uppercase tracking-[.14em] text-muted backdrop-blur">
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
                                        class="cursor-pointer border-t border-border-soft hover:bg-primary/[.06]"
                                        onClick={() => openJoin(server)}
                                    >
                                        <td class="px-[length:var(--space-xl)] py-3.5">
                                            <div class="flex min-w-0 items-center gap-3">
                                                <ServerAvatar server={server} class="size-10 text-sm" />
                                                <div class="min-w-0">
                                                    <div class="truncate">{server.name}</div>
                                                    <div class="text-[length:var(--text-label)] text-muted">
                                                        {server.address}
                                                    </div>
                                                </div>
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
