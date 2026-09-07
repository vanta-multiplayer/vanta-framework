import type {JSX} from "solid-js";

export function VolumeMeter(props: {label: string; value: number}): JSX.Element {
    const width = () => Math.max(0, Math.min(100, props.value));

    return (
        <label class="block">
            <span class="mb-4 block text-[length:var(--text-h3)] font-medium tracking-tight text-muted">
                {props.label}
            </span>
            <span class="block h-1 w-full overflow-hidden rounded-sm bg-foreground/15">
                <span
                    class="block h-full rounded-sm bg-primary-hover transition-[width] duration-75"
                    style={{width: `${width()}%`}}
                />
            </span>
        </label>
    );
}
