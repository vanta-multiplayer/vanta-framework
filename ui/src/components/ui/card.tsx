import type {JSX} from "solid-js";
import {cn} from "../../lib/cn";

export function Card(props: {class?: string; children: JSX.Element}): JSX.Element {
    return (
        <div class={cn("rounded-md border border-border bg-white/[.025] p-6", props.class)}>
            {props.children}
        </div>
    );
}
