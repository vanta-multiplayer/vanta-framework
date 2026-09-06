import type {JSX} from "solid-js";
import {For, Show} from "solid-js";
import {A} from "@solidjs/router";
import {Field} from "../form/Field";
import {Button} from "../form/Button";
import {routes} from "../../routes";
import {
    address,
    connectTo,
    favorites,
    nickname,
    persistNickname,
    recent,
    setAddress,
    setNickname,
} from "../../state/session";
import type {ServerEntry} from "../../state/bridge";

function ServerCard(props: {server: ServerEntry}): JSX.Element {
    return (
        <button type="button" class="server-card" onClick={() => connectTo(props.server.address)}>
            <span class="server-card__name">{props.server.name}</span>
            <span class="server-card__players">
                {props.server.players} <span>/ {props.server.maxPlayers}</span>
            </span>
        </button>
    );
}

function Group(props: {title: string; servers: ServerEntry[]; empty: string}): JSX.Element {
    return (
        <section class="flex flex-col gap-[length:var(--space-lg)]">
            <h3>{props.title}</h3>
            <Show
                when={props.servers.length > 0}
                fallback={<p class="m-0 text-[length:var(--text-h2)] leading-[1.4] font-medium text-muted">{props.empty}</p>}
            >
                <div class="flex flex-col gap-4">
                    <For each={props.servers}>{(server) => <ServerCard server={server} />}</For>
                </div>
            </Show>
        </section>
    );
}

export function Home(): JSX.Element {
    return (
        <div class="view-pad flex h-full min-h-0 flex-col overflow-y-auto">
            <header class="mb-[length:var(--space-xl)] flex min-h-[length:var(--header-row)] items-end">
                <h1>Welcome to Vanta</h1>
            </header>

            <div class="mb-[length:var(--space-xl)] max-w-md">
                <Field
                    id="nickname"
                    label="Name"
                    value={nickname()}
                    maxLength={24}
                    onInput={setNickname}
                    onBlur={() => persistNickname(nickname())}
                />
                <div class="mt-8 flex items-end gap-4">
                    <div class="min-w-0 flex-1">
                        <Field
                            id="last-address"
                            label="Last server"
                            value={address()}
                            placeholder="host:port"
                            onInput={setAddress}
                            onEnter={() => connectTo(address())}
                        />
                    </div>
                    <Button kind="primary" onClick={() => connectTo(address())}>
                        Connect
                    </Button>
                </div>
            </div>

            <div class="flex flex-col gap-[length:var(--space-xl)]">
                <Group title="Recent" servers={recent()} empty="Join a server and it will show up here." />
                <Group title="Favorites" servers={favorites()} empty="No favorites yet." />
            </div>

            <A href={routes.servers} class="mt-10 text-muted hover:text-foreground">
                Browse the server list
            </A>
        </div>
    );
}
