import type {JSX} from "solid-js";
import {Show, createEffect, createSignal} from "solid-js";
import {cn} from "../lib/cn";
import {serverLetter, serverPalette} from "../lib/server-art";
import type {ServerEntry} from "../state/bridge";
import {Logo} from "./Brand";

function FallbackArt(props: {server: ServerEntry; class?: string}): JSX.Element {
    const palette = () => serverPalette(props.server);

    return (
        <div
            class={cn("relative overflow-hidden", props.class)}
            style={{background: `linear-gradient(135deg, ${palette().from} 0%, ${palette().to} 100%)`}}
        >
            <div class="hero-grid pointer-events-none absolute inset-0 opacity-50" />
            <Logo class="pointer-events-none absolute top-1/2 right-[-6%] w-[46%] -translate-y-1/2 opacity-25" alt="" />
        </div>
    );
}

export function ServerBanner(props: {server: ServerEntry; class?: string}): JSX.Element {
    const [failed, setFailed] = createSignal(false);

    createEffect(() => {
        props.server.banner;
        setFailed(false);
    });

    return (
        <Show
            when={props.server.banner && !failed()}
            fallback={<FallbackArt server={props.server} class={props.class} />}
        >
            <img
                src={props.server.banner}
                alt=""
                class={cn("size-full object-cover", props.class)}
                onError={() => setFailed(true)}
            />
        </Show>
    );
}

export function ServerAvatar(props: {server: ServerEntry; class?: string}): JSX.Element {
    const [failed, setFailed] = createSignal(false);
    const palette = () => serverPalette(props.server);

    createEffect(() => {
        props.server.avatar;
        setFailed(false);
    });

    return (
        <div
            class={cn(
                "grid shrink-0 place-items-center overflow-hidden rounded-full border border-white/15 bg-surface-raised font-display font-bold text-white shadow-[0_10px_30px_rgba(0,0,0,.35)]",
                props.class,
            )}
            style={
                props.server.avatar && !failed()
                    ? undefined
                    : {background: `linear-gradient(145deg, ${palette().from}, ${palette().to})`}
            }
        >
            <Show
                when={props.server.avatar && !failed()}
                fallback={<span class="leading-none">{serverLetter(props.server)}</span>}
            >
                <img
                    src={props.server.avatar}
                    alt=""
                    class="size-full object-cover"
                    onError={() => setFailed(true)}
                />
            </Show>
        </div>
    );
}
