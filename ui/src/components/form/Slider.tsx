import type {JSX} from "solid-js";
import {createEffect} from "solid-js";
import {Label} from "../ui/label";

export function Slider(props: {
    id?: string;
    label: string;
    value: number;
    min?: number;
    max?: number;
    suffix?: string;
    onChange: (value: number) => void;
}): JSX.Element {
    const min = () => props.min ?? 0;
    const max = () => props.max ?? 100;
    const fill = () => {
        const span = max() - min();
        return span <= 0 ? 0 : ((props.value - min()) / span) * 100;
    };

    let input: HTMLInputElement | undefined;

    createEffect(() => {
        if (input) {
            input.style.setProperty("--fill", `${fill()}%`);
        }
    });

    return (
        <div>
            <Label for={props.id}>
                {props.label}
                <span class="ml-2 font-mono text-[11px] tracking-normal text-foreground normal-case">
                    {Math.round(props.value)}
                    {props.suffix ?? "%"}
                </span>
            </Label>
            <input
                ref={input}
                id={props.id}
                type="range"
                min={min()}
                max={max()}
                step="1"
                value={props.value}
                class="h-1 w-full cursor-grab appearance-none rounded-sm outline-none active:cursor-grabbing
                       [&::-webkit-slider-runnable-track]:h-1 [&::-webkit-slider-runnable-track]:appearance-none
                       [&::-webkit-slider-runnable-track]:bg-transparent
                       [&::-webkit-slider-thumb]:relative [&::-webkit-slider-thumb]:z-10
                       [&::-webkit-slider-thumb]:mt-[-8px] [&::-webkit-slider-thumb]:size-4
                       [&::-webkit-slider-thumb]:appearance-none [&::-webkit-slider-thumb]:rounded-full
                       [&::-webkit-slider-thumb]:border-2 [&::-webkit-slider-thumb]:border-primary
                       [&::-webkit-slider-thumb]:bg-background"
                style={{
                    background:
                        "linear-gradient(to right, var(--color-primary), var(--color-primary) var(--fill), rgb(255 255 255 / 0.12) var(--fill))",
                }}
                onInput={(event) => props.onChange(Number.parseInt(event.currentTarget.value, 10) || 0)}
            />
        </div>
    );
}
