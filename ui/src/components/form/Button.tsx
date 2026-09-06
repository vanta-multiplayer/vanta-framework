import type {JSX} from "solid-js";

export function Button(props: {
    children: JSX.Element;
    type?: "button" | "submit";
    kind?: "primary" | "ghost" | "danger" | "success";
    disabled?: boolean;
    onClick?: () => void;
}): JSX.Element {
    const kind = () => props.kind ?? "ghost";

    return (
        <button
            type={props.type ?? "button"}
            disabled={props.disabled}
            onClick={props.onClick}
            class="text-body flex h-[length:var(--control-height)] cursor-pointer items-center justify-center
                   rounded-lg border px-4 font-medium whitespace-nowrap
                   transition-transform active:not-disabled:scale-95
                   disabled:cursor-default disabled:opacity-50"
            classList={{
                "border-primary bg-primary text-foreground hover:not-disabled:bg-primary-hover": kind() === "primary",
                "border-success bg-surface text-foreground": kind() === "success",
                "border-danger bg-surface text-foreground": kind() === "danger",
                "border-border bg-surface text-foreground": kind() === "ghost",
            }}
        >
            {props.children}
        </button>
    );
}
