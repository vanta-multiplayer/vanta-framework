import type {JSX} from "solid-js";
import {cn} from "../../lib/cn";

export function Badge(props: {class?: string; children: JSX.Element}): JSX.Element {
    return (
        <span
            class={cn(
                "inline-flex items-center gap-2 rounded-full border border-border bg-white/[.025] px-3 py-1.5 text-[11px] font-bold uppercase tracking-[.16em] text-muted",
                props.class,
            )}
        >
            {props.children}
        </span>
    );
}
