import type {JSX} from "solid-js";

export function Checkbox(props: {
    label: string;
    checked: boolean;
    onChange: (checked: boolean) => void;
}): JSX.Element {
    return (
        <label class="flex cursor-pointer items-center gap-4 text-[length:var(--text-h3)] font-medium text-foreground">
            <span class="relative size-6 shrink-0 rounded-[5px] bg-foreground/15">
                <input
                    type="checkbox"
                    class="peer absolute size-0 opacity-0"
                    checked={props.checked}
                    onChange={(event) => props.onChange(event.currentTarget.checked)}
                />
                <span class="pointer-events-none absolute top-1 left-2 h-2.5 w-1.5 rotate-45 border-r-2 border-b-2 border-primary opacity-0 peer-checked:opacity-100" />
            </span>
            {props.label}
        </label>
    );
}
