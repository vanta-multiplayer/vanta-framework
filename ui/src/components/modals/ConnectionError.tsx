import type {JSX} from "solid-js";
import {Show, onCleanup, onMount} from "solid-js";
import {address, connectTo, errorMessage, errorTitle, setErrorOpen} from "../../state/session";
import {Button} from "../form/Button";
import {Overlay} from "./Overlay";

export function ConnectionError(): JSX.Element {
    function close(): void {
        setErrorOpen(false);
    }

    function retry(): void {
        close();
        connectTo(address());
    }

    onMount(() => {
        function onKeyDown(event: KeyboardEvent): void {
            if (event.key === "Enter") {
                event.preventDefault();
                retry();
            }

            if (event.key === "Escape") {
                event.preventDefault();
                close();
            }
        }

        window.addEventListener("keydown", onKeyDown);
        onCleanup(() => window.removeEventListener("keydown", onKeyDown));
    });

    return (
        <Overlay class="border-danger/40" onClose={close}>
            <div class="text-center">
                <div class="font-display text-3xl font-bold tracking-tight text-danger">{errorTitle()}</div>
                <Show when={address()}>
                    <div class="mt-2 text-sm font-semibold text-muted">{address()}</div>
                </Show>
                <p class="mt-4 mb-0 text-sm leading-relaxed whitespace-pre-line text-foreground">
                    {errorMessage()}
                </p>
                <div class="mt-8 flex justify-center gap-3">
                    <Button variant="primary" onClick={retry}>
                        Retry
                    </Button>
                    <Button variant="secondary" onClick={close}>
                        Close
                    </Button>
                </div>
            </div>
        </Overlay>
    );
}
