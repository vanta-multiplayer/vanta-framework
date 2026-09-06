import type {JSX} from "solid-js";
export function Field(props: {
    id: string;
    label?: string;
    value: string;
    placeholder?: string;
    maxLength?: number;
    type?: "text" | "password" | "search";
    disabled?: boolean;
    onInput: (value: string) => void;
    onEnter?: () => void;
    onBlur?: () => void;
}): JSX.Element {
    return (
        <label class="block" for={props.id}>
            {props.label ? (
                <span class="mb-3 block text-[length:var(--text-h3)] font-medium tracking-tight text-muted">
                    {props.label}
                </span>
            ) : null}
            <input
                id={props.id}
                class="text-body h-[length:var(--control-height)] w-full rounded-lg border border-border
                       bg-brown-dark px-3.5 text-foreground outline-none select-text
                       placeholder:text-muted/50 focus:border-primary
                       disabled:opacity-50"
                type={props.type ?? "text"}
                value={props.value}
                maxLength={props.maxLength}
                placeholder={props.placeholder}
                disabled={props.disabled}
                spellcheck={false}
                autocomplete="off"
                onInput={(event) => props.onInput(event.currentTarget.value)}
                onBlur={() => props.onBlur?.()}
                onKeyDown={(event) => {
                    if (event.key === "Enter") {
                        props.onEnter?.();
                    }
                }}
            />
        </label>
    );
}
