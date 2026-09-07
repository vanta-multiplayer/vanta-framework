import type {JSX} from "solid-js";
import {cn} from "../../lib/cn";

export function Dialog(props: {children: JSX.Element; class?: string; onClose?: () => void}): JSX.Element {
    return (
        <div
            class="pointer-events-auto fixed inset-0 z-50 flex items-center justify-center bg-background/80"
            onMouseDown={(event) => {
                if (event.target === event.currentTarget) {
                    props.onClose?.();
                }
            }}
        >
            <div class={cn("glass w-[min(28rem,90vw)] rounded-md border border-border p-8 shadow-2xl", props.class)}>
                {props.children}
            </div>
        </div>
    );
}
