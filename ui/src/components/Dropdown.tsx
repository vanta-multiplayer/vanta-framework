import type {JSX} from "solid-js";
import { For, Show, createSignal, onCleanup } from "solid-js";
import { Portal } from "solid-js/web";
import ChevronDown from "lucide-solid/icons/chevron-down";

export interface DropdownOption {
    value: string;
    label: string;
}

export interface DropdownProps {
    options: DropdownOption[];
    value: string;
    disabled?: boolean;
    unknownLabel?: string;
    onChange: (value: string) => void;
}

/**
 * A dropdown that works under off-screen rendering.
 *
 * WHY NOT `<select>`
 * A native select's popup is an OS-drawn window. CEF's off-screen renderer has
 * no window to draw it into, so the control highlights and never opens.
 *
 * The list is portalled and `position: fixed` so a parent with overflow (the
 * settings column) cannot clip it. pointer-events must be set on the list
 * itself: body and #root are click-through so the game shows through.
 */
export function Dropdown(props: DropdownProps): JSX.Element {
    const [open, setOpen] = createSignal(false);
    const [box, setBox] = createSignal<DOMRect | null>(null);

    let trigger: HTMLButtonElement | undefined;
    let menu: HTMLDivElement | undefined;

    function measure(): void {
        if (trigger) {
            setBox(trigger.getBoundingClientRect());
        }
    }

    function close(): void {
        setOpen(false);
    }

    function toggle(): void {
        if (props.disabled) {
            return;
        }

        if (open()) {
            close();
            return;
        }

        measure();
        setOpen(true);
    }

    function onDocumentPointer(event: PointerEvent): void {
        const target = event.target as Node | null;

        if (target && (trigger?.contains(target) || menu?.contains(target))) {
            return;
        }

        close();
    }

    document.addEventListener("pointerdown", onDocumentPointer);
    window.addEventListener("resize", close);
    onCleanup(() => {
        document.removeEventListener("pointerdown", onDocumentPointer);
        window.removeEventListener("resize", close);
    });

    const selected = (): DropdownOption | undefined =>
        props.options.find((option) => option.value === props.value);

    return (
        <>
            <button
                ref={trigger}
                type="button"
                disabled={props.disabled}
                class="text-body flex h-[length:var(--control-height)] w-full items-center justify-between
                       rounded-lg border border-border bg-brown-dark px-3.5 text-left text-foreground
                       disabled:opacity-50"
                onClick={toggle}
            >
                <span>{selected()?.label ?? props.unknownLabel ?? "Select"}</span>
                <ChevronDown class={`size-4 text-muted transition-transform ${open() ? "rotate-180" : ""}`} />
            </button>

            <Show when={open() && box()}>
                {(rect) => (
                    <Portal>
                        <div
                            ref={menu}
                            class="pointer-events-auto fixed z-[200] max-h-56 overflow-y-auto rounded-lg
                                   border border-border bg-surface-elevated shadow-xl shadow-brown-dark/60"
                            style={{
                                top: `${rect().bottom + 4}px`,
                                left: `${rect().left}px`,
                                width: `${rect().width}px`,
                            }}
                        >
                            <For each={props.options}>
                                {(option) => (
                                    <button
                                        type="button"
                                        class="text-body block w-full px-3.5 py-2.5 text-left text-foreground hover:bg-surface"
                                        classList={{ "bg-brown": option.value === props.value }}
                                        onClick={() => {
                                            props.onChange(option.value);
                                            close();
                                        }}
                                    >
                                        {option.label}
                                    </button>
                                )}
                            </For>
                        </div>
                    </Portal>
                )}
            </Show>
        </>
    );
}
