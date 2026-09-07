import type {JSX} from "solid-js";
import {For, Show, createEffect, createSignal} from "solid-js";
import Users from "lucide-solid/icons/users";
import Globe from "lucide-solid/icons/globe";
import Gamepad2 from "lucide-solid/icons/gamepad-2";
import Link from "lucide-solid/icons/link";
import X from "lucide-solid/icons/x";
import {
    closeJoin,
    connectTo,
    joinServer,
    nickname,
    persistNickname,
    setNickname,
} from "../../state/session";
import {Button} from "../form/Button";
import {Field} from "../form/Field";
import {ServerAvatar, ServerBanner} from "../ServerIdentity";
import {Overlay} from "./Overlay";

export function JoinModal(): JSX.Element {
    const [password, setPassword] = createSignal("");
    const server = () => joinServer();

    createEffect(() => {
        joinServer();
        setPassword("");
    });

    function play(): void {
        const current = server();

        if (!current) {
            return;
        }

        persistNickname(nickname());
        connectTo(current.address, password());
    }

    return (
        <Show when={server()}>
            {(current) => (
                <Overlay class="w-[min(52rem,94vw)] overflow-hidden p-0" onClose={closeJoin}>
                    <div class="relative h-56">
                        <ServerBanner server={current()} class="absolute inset-0 size-full" />
                        <div class="absolute inset-0 bg-gradient-to-t from-background via-background/55 to-black/10" />

                        <ServerAvatar
                            server={current()}
                            class="absolute top-5 left-6 size-16 text-2xl"
                        />
                        <button
                            type="button"
                            title="Close"
                            class="absolute top-4 right-4 grid size-9 place-items-center rounded-md text-white/80 hover:bg-black/30 hover:text-white"
                            onClick={closeJoin}
                        >
                            <X class="size-5" />
                        </button>

                        <div class="absolute inset-x-6 bottom-5 flex items-end justify-between gap-4">
                            <div class="min-w-0">
                                <h2 class="truncate font-display text-[2rem] font-bold tracking-[-.04em] text-white">
                                    {current().name}
                                </h2>
                                <p class="mt-1 flex items-center gap-2 text-sm font-semibold text-white/75">
                                    <Users class="size-4" />
                                    {current().players} / {current().maxPlayers} online
                                    <span class="text-white/40">·</span>
                                    <span class="truncate">{current().address}</span>
                                </p>
                            </div>
                            <Button variant="primary" class="shrink-0 px-8" onClick={play}>
                                Play
                            </Button>
                        </div>
                    </div>

                    <div class="flex flex-col gap-6 px-6 py-6">
                        <Show
                            when={
                                current().language || current().gamemode || current().website
                            }
                        >
                            <div class="grid grid-cols-3 gap-4">
                                <Meta
                                    icon={<Globe class="size-4" />}
                                    label="Language"
                                    value={current().language}
                                />
                                <Meta
                                    icon={<Gamepad2 class="size-4" />}
                                    label="Game mode"
                                    value={current().gamemode}
                                />
                                <Meta
                                    icon={<Link class="size-4" />}
                                    label="Website"
                                    value={current().website}
                                />
                            </div>
                        </Show>

                        <Show when={(current().tags ?? []).length > 0}>
                            <div>
                                <p class="mb-2 text-[11px] font-bold uppercase tracking-[.16em] text-muted">
                                    Tags
                                </p>
                                <div class="flex flex-wrap gap-2">
                                    <For each={[...(current().tags ?? [])]}>
                                        {(tag) => (
                                            <span class="rounded-full border border-border bg-white/[.04] px-2.5 py-1 text-xs font-semibold text-foreground">
                                                {tag}
                                            </span>
                                        )}
                                    </For>
                                </div>
                            </div>
                        </Show>

                        <Show when={current().description}>
                            <div>
                                <p class="mb-2 text-[11px] font-bold uppercase tracking-[.16em] text-muted">
                                    Description
                                </p>
                                <p class="m-0 text-sm leading-6 text-foreground/90">
                                    {current().description}
                                </p>
                            </div>
                        </Show>

                        <div class="flex items-end gap-3 border-t border-border pt-5">
                            <div class="min-w-0 flex-1">
                                <Field
                                    id="join-nickname"
                                    label="Name"
                                    value={nickname()}
                                    maxLength={24}
                                    onInput={setNickname}
                                    onBlur={() => persistNickname(nickname())}
                                    onEnter={play}
                                />
                            </div>
                            <div class="min-w-0 flex-1">
                                <Field
                                    id="join-password"
                                    type="password"
                                    label="Password"
                                    value={password()}
                                    placeholder="If needed"
                                    onInput={setPassword}
                                    onEnter={play}
                                />
                            </div>
                        </div>
                    </div>
                </Overlay>
            )}
        </Show>
    );
}

function Meta(props: {icon: JSX.Element; label: string; value?: string}): JSX.Element {
    return (
        <div class="min-w-0">
            <p class="mb-1 flex items-center gap-1.5 text-[11px] font-bold uppercase tracking-[.16em] text-muted">
                {props.icon}
                {props.label}
            </p>
            <p class="m-0 truncate text-sm font-semibold text-foreground">{props.value || "—"}</p>
        </div>
    );
}
