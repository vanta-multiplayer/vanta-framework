import type {JSX} from "solid-js";

export function Checkbox(props: {
    label: string;
    checked: boolean;
    onChange: (checked: boolean) => void;
}): JSX.Element {
    return (
        <label class="flex cursor-pointer items-center gap-3 text-sm font-semibold text-foreground">
            <span class="relative size-4 shrink-0 rounded-[4px] border border-border bg-surface-raised">
                <input
                    type="checkbox"
                    class="peer absolute size-0 opacity-0"
                    checked={props.checked}
                    onChange={(event) => props.onChange(event.currentTarget.checked)}
                />
                <span class="pointer-events-none absolute top-0.5 left-1 h-2 w-1 rotate-45 border-r-2 border-b-2 border-primary opacity-0 peer-checked:opacity-100" />
            </span>
            {props.label}
        </label>
    );
}
