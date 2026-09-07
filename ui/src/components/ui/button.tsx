import type {JSX} from "solid-js";
import {splitProps} from "solid-js";
import {cn} from "../../lib/cn";

export type ButtonVariant = "primary" | "secondary" | "ghost" | "danger" | "success";

const variants: Record<ButtonVariant, string> = {
    primary:
        "border-transparent bg-primary text-[#180b03] hover:not-disabled:bg-primary-soft shadow-[0_0_0_1px_rgba(255,255,255,.06),0_12px_40px_rgba(242,139,58,.16)]",
    secondary:
        "border-border bg-white/[.025] text-foreground hover:not-disabled:border-primary/50 hover:not-disabled:bg-primary/[.06]",
    ghost: "border-transparent bg-transparent text-muted hover:not-disabled:text-foreground hover:not-disabled:bg-white/[.04]",
    danger: "border-danger/40 bg-danger/10 text-foreground hover:not-disabled:bg-danger/20",
    success: "border-success/40 bg-success/10 text-foreground hover:not-disabled:bg-success/20",
};

export function Button(
    props: {
        children: JSX.Element;
        type?: "button" | "submit";
        variant?: ButtonVariant;
        kind?: ButtonVariant | "ghost";
        disabled?: boolean;
        class?: string;
        onClick?: () => void;
    } & JSX.ButtonHTMLAttributes<HTMLButtonElement>,
): JSX.Element {
    const [local, rest] = splitProps(props, ["children", "variant", "kind", "class", "type"]);
    const variant = () => local.variant ?? local.kind ?? "secondary";

    return (
        <button
            {...rest}
            type={local.type ?? "button"}
            class={cn(
                "inline-flex min-h-[length:var(--control-height)] cursor-pointer items-center justify-center gap-2 rounded-md border px-5 text-sm font-extrabold tracking-[-.01em] whitespace-nowrap transition-all duration-200 active:not-disabled:translate-y-px disabled:cursor-default disabled:opacity-50",
                variants[variant()],
                local.class,
            )}
        >
            {local.children}
        </button>
    );
}
