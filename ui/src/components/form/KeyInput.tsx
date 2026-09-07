import type {JSX} from "solid-js";
import {createSignal, onCleanup} from "solid-js";

const KEY_NAMES: Record<number, string> = {
    8: "Backspace",
    9: "Tab",
    13: "Enter",
    16: "Shift",
    17: "Ctrl",
    18: "Alt",
    20: "Caps Lock",
    27: "Esc",
    32: "Space",
    33: "Page Up",
    34: "Page Down",
    35: "End",
    36: "Home",
    37: "Left",
    38: "Up",
    39: "Right",
    40: "Down",
    45: "Insert",
    46: "Delete",
    112: "F1",
    113: "F2",
    114: "F3",
    115: "F4",
    116: "F5",
    117: "F6",
    118: "F7",
    119: "F8",
    120: "F9",
    121: "F10",
    122: "F11",
    123: "F12",
};

function nameFor(code: number): string {
    if (KEY_NAMES[code]) {
        return KEY_NAMES[code];
    }

    if (code >= 48 && code <= 90) {
        return String.fromCharCode(code);
    }

    return `VK ${code}`;
}

export function KeyInput(props: {value: number; onChange: (value: number) => void}): JSX.Element {
    const [listening, setListening] = createSignal(false);

    function onKey(event: KeyboardEvent): void {
        if (!listening()) {
            return;
        }

        event.preventDefault();
        event.stopPropagation();

        if (event.keyCode < 8) {
            return;
        }

        props.onChange(event.keyCode);
        setListening(false);
    }

    window.addEventListener("keydown", onKey, true);
    onCleanup(() => window.removeEventListener("keydown", onKey, true));

    return (
        <button
            type="button"
            class="min-w-20 rounded-md border border-border bg-surface-raised px-4
                   h-[length:var(--control-height)] text-sm font-extrabold text-foreground"
            classList={{"border-primary bg-primary/10": listening()}}
            onClick={(event) => {
                event.stopPropagation();
                setListening(!listening());
            }}
        >
            {listening() ? "…" : nameFor(props.value)}
        </button>
    );
}
