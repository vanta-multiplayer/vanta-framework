import type {JSX} from "solid-js";
import {splitProps} from "solid-js";
import {cn} from "../../lib/cn";

export function Input(
    props: JSX.InputHTMLAttributes<HTMLInputElement> & {class?: string},
): JSX.Element {
    const [local, rest] = splitProps(props, ["class"]);

    return (
        <input
            {...rest}
            class={cn(
                "h-[length:var(--control-height)] w-full rounded-md border border-border bg-surface-raised px-3.5 text-sm text-foreground outline-none select-text placeholder:text-muted/50 focus-visible:border-primary focus-visible:ring-2 focus-visible:ring-primary/20 disabled:opacity-50",
                local.class,
            )}
        />
    );
}
