import type {JSX} from "solid-js";
import {For, Show} from "solid-js";
import {A} from "@solidjs/router";
import {Badge} from "../ui/badge";
import {Button} from "../form/Button";
import {Logo, Wordmark} from "../Brand";
import {ServerAvatar} from "../ServerIdentity";
import {routes} from "../../routes";
import {favorites, goTo, openJoin, recent} from "../../state/session";
import type {ServerEntry} from "../../state/bridge";

function ServerCard(props: {server: ServerEntry}): JSX.Element {
    return (
        <button type="button" class="server-card" onClick={() => openJoin(props.server)}>
            <ServerAvatar server={props.server} class="size-9 text-sm" />
            <span class="server-card__name">{props.server.name}</span>
            <span class="server-card__players">
                {props.server.players} <span>/ {props.server.maxPlayers}</span>
            </span>
        </button>
    );
}

function Group(props: {title: string; servers: ServerEntry[]; empty: string}): JSX.Element {
    return (
        <section class="flex flex-col gap-4">
            <h3>{props.title}</h3>
            <Show
                when={props.servers.length > 0}
                fallback={<p class="m-0 text-sm leading-relaxed text-muted">{props.empty}</p>}
            >
                <div class="flex flex-col gap-2">
                    <For each={props.servers}>{(server) => <ServerCard server={server} />}</For>
                </div>
            </Show>
        </section>
    );
}

export function Home(): JSX.Element {
    return (
        <div class="relative flex h-full min-h-0 overflow-hidden">
            <div class="hero-grid pointer-events-none absolute inset-0 opacity-50" />
            <div class="pointer-events-none absolute left-[18%] top-[8%] size-[22rem] rounded-full bg-primary/[.08] blur-[110px]" />
            <div class="pointer-events-none absolute right-[8%] bottom-[10%] size-[18rem] rounded-full bg-primary-deep/[.08] blur-[120px]" />

            <div class="relative grid min-h-0 w-full grid-cols-[minmax(0,1.1fr)_minmax(16rem,0.9fr)] items-center gap-12 overflow-y-auto px-[length:var(--space-xl)] py-12">
                <div class="max-w-2xl">
                    <Badge class="mb-6">
                        <i class="size-1.5 shrink-0 rounded-full bg-primary not-italic" />
                        Early development
                    </Badge>
                    <div class="mb-6 flex items-center gap-3">
                        <Wordmark class="text-2xl" />
                    </div>
                    <h1 class="font-display text-[clamp(2.8rem,6vw,5.6rem)] font-bold leading-[.82] tracking-[-.07em]">
                        <span class="block text-white/95">San Andreas.</span>
                        <span class="text-gradient block">Built forward.</span>
                    </h1>
                    <p class="mt-6 max-w-xl text-sm leading-7 text-muted">
                        Pick a recent or favorite server, or browse the list. Direct connect is in the nav if you already have an address.
                    </p>

                    <div class="mt-8">
                        <Button variant="primary" onClick={() => goTo(routes.servers)}>
                            Browse servers
                        </Button>
                    </div>

                    <div class="mt-10 flex flex-col gap-8">
                        <Group title="Recent" servers={recent()} empty="Join a server and it will show up here." />
                        <Group title="Favorites" servers={favorites()} empty="No favorites yet." />
                    </div>

                    <A href={routes.servers} class="mt-8 inline-block text-sm font-semibold text-muted hover:text-foreground">
                        Open the server list
                    </A>
                </div>

                <div class="relative mx-auto hidden aspect-square w-full max-w-[28rem] items-center justify-center md:flex">
                    <div class="absolute size-[70%] rounded-full border border-primary/10" />
                    <div class="absolute size-[89%] rounded-full border border-dashed border-primary/10" />
                    <div class="absolute size-[58%] rounded-full bg-primary/[.07] blur-3xl" />
                    <Logo alt="Vanta" class="logo-shadow relative z-10 w-[72%]" />
                </div>
            </div>
        </div>
    );
}
