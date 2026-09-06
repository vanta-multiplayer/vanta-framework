import type {JSX} from "solid-js";

export function Overlay(props: {children: JSX.Element; onClose?: () => void}): JSX.Element {
    return (
        <div
            class="pointer-events-auto fixed inset-0 z-50 flex items-center justify-center bg-background/90"
            onMouseDown={(event) => {
                if (event.target === event.currentTarget) {
                    props.onClose?.();
                }
            }}
        >
            {props.children}
        </div>
    );
}
