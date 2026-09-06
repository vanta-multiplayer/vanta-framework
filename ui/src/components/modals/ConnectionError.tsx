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
        <Overlay onClose={close}>
            <div class="w-[min(32rem,90vw)] rounded-lg border-2 border-danger/50 bg-surface px-[length:var(--space-xl)] py-[length:var(--space-xl)] text-center">
                <div class="flex flex-col items-center gap-6">
                    <div class="text-[length:var(--text-h1)] font-semibold text-danger">{errorTitle()}</div>
                    <Show when={address()}>
                        <div class="text-[length:var(--text-h3)] font-medium text-muted">{address()}</div>
                    </Show>
                    <p class="m-0 max-w-md text-[length:var(--text-body)] leading-relaxed text-foreground whitespace-pre-line">
                        {errorMessage()}
                    </p>
                </div>
                <div class="mt-[length:var(--space-xl)] flex justify-center gap-4">
                    <Button kind="primary" onClick={retry}>
                        Retry
                    </Button>
                    <Button kind="ghost" onClick={close}>
                        Close
                    </Button>
                </div>
            </div>
        </Overlay>
    );
}
