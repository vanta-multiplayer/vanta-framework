import type {JSX} from "solid-js";
import logo from "../assets/vanta-logo.png";
import {cn} from "../lib/cn";

export function Logo(props: {class?: string; alt?: string}): JSX.Element {
    return <img src={logo} alt={props.alt ?? ""} class={cn("object-contain", props.class)} />;
}

export function Wordmark(props: {class?: string}): JSX.Element {
    return (
        <span class={cn("font-display font-bold tracking-[-.04em] text-white", props.class)}>
            vanta<span class="text-primary">.mp</span>
        </span>
    );
}

export function Brand(props: {class?: string; markClass?: string}): JSX.Element {
    return (
        <div class={cn("flex items-center gap-3", props.class)}>
            <Logo class={cn("size-10", props.markClass)} alt="" />
            <Wordmark class="text-[19px]" />
        </div>
    );
}
