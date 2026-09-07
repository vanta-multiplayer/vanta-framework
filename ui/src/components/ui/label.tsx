import type {JSX} from "solid-js";
import {cn} from "../../lib/cn";

export function Label(props: {class?: string; for?: string; children: JSX.Element}): JSX.Element {
    return (
        <label
            for={props.for}
            class={cn("mb-3 block text-xs font-bold uppercase tracking-[.14em] text-muted", props.class)}
        >
            {props.children}
        </label>
    );
}
