import type {JSX} from "solid-js";
import {Label} from "../ui/label";

export function VolumeMeter(props: {label: string; value: number}): JSX.Element {
    const width = () => Math.max(0, Math.min(100, props.value));

    return (
        <div>
            <Label>{props.label}</Label>
            <span class="block h-1.5 w-full overflow-hidden rounded-sm bg-white/10">
                <span
                    class="block h-full rounded-sm bg-primary transition-[width] duration-75"
                    style={{width: `${width()}%`}}
                />
            </span>
        </div>
    );
}
