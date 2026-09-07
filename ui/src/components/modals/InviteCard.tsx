import type {JSX} from "solid-js";
import {For, Show} from "solid-js";
import Users from "lucide-solid/icons/users";
import Globe from "lucide-solid/icons/globe";
import Link from "lucide-solid/icons/link";
import X from "lucide-solid/icons/x";
import {closeInvite, connectTo, inviteServer, nickname, persistNickname} from "../../state/session";
import {Button} from "../form/Button";
import {ServerAvatar} from "../ServerIdentity";

function Pill(props: {icon: JSX.Element; children: JSX.Element}): JSX.Element {
    return (
        <span class="inline-flex items-center gap-1.5 rounded-full border border-white/10 bg-white/[.06] px-2.5 py-1 text-xs font-semibold text-white/85">
            {props.icon}
            {props.children}
        </span>
    );
}

export function InviteCard(): JSX.Element {
    const server = () => inviteServer();

    function play(): void {
        const current = server();

        if (!current) {
            return;
        }

        persistNickname(nickname());
        connectTo(current.address);
    }

    return (
        <Show when={server()}>
            {(current) => (
                <div class="pointer-events-auto fixed inset-0 z-50">
                    <button
                        type="button"
                        class="absolute inset-0 cursor-default bg-black/20"
                        aria-label="Dismiss invite"
                        onClick={closeInvite}
                    />
                    <div class="absolute top-1/2 left-[6%] w-[min(24rem,86vw)] -translate-y-1/2 rounded-2xl border border-white/10 bg-black/55 p-7 shadow-[0_24px_80px_rgba(0,0,0,.45)] backdrop-blur-xl">
                        <button
                            type="button"
                            title="Close"
                            class="absolute top-4 right-4 grid size-8 place-items-center rounded-md text-white/55 hover:bg-white/10 hover:text-white"
                            onClick={closeInvite}
                        >
                            <X class="size-4" />
                        </button>

                        <ServerAvatar server={current()} class="size-[4.25rem] text-2xl" />

                        <h2 class="mt-5 pr-8 font-display text-[1.85rem] font-bold leading-none tracking-[-.04em] text-white">
                            {current().name}
                        </h2>

                        <p class="mt-3 flex items-center gap-2 text-sm font-semibold text-white/70">
                            <Users class="size-4 text-primary" />
                            {current().players} / {current().maxPlayers || "—"} online
                        </p>

                        <Show when={current().website || current().language}>
                            <div class="mt-3 flex flex-wrap gap-2">
                                <Show when={current().website}>
                                    <Pill icon={<Link class="size-3.5" />}>{current().website}</Pill>
                                </Show>
                                <Show when={current().language}>
                                    <Pill icon={<Globe class="size-3.5" />}>{current().language}</Pill>
                                </Show>
                            </div>
                        </Show>

                        <Show when={(current().tags ?? []).length > 0}>
                            <div class="mt-3 flex flex-wrap gap-2">
                                <For each={[...(current().tags ?? [])]}>
                                    {(tag) => (
                                        <span class="rounded-full border border-white/10 bg-white/[.04] px-2.5 py-1 text-xs font-semibold text-white/70">
                                            {tag}
                                        </span>
                                    )}
                                </For>
                            </div>
                        </Show>

                        <Show when={current().description}>
                            <p class="mt-4 mb-0 text-sm leading-6 text-white/80">{current().description}</p>
                        </Show>

                        <Button variant="primary" class="mt-8 px-8" onClick={play}>
                            Play
                        </Button>
                    </div>
                </div>
            )}
        </Show>
    );
}
