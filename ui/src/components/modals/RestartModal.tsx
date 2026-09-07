import type {JSX} from "solid-js";
import {onCleanup, onMount} from "solid-js";
import {emit} from "../../state/bridge";
import {setRestartOpen} from "../../state/session";
import {Button} from "../form/Button";
import {Overlay} from "./Overlay";

export function RestartModal(): JSX.Element {
    function leave(): void {
        emit("exit");
    }

    onMount(() => {
        function onKeyDown(event: KeyboardEvent): void {
            if (event.key === "Enter") {
                event.preventDefault();
                leave();
            }

            if (event.key === "Escape") {
                event.preventDefault();
                setRestartOpen(false);
            }
        }

        window.addEventListener("keydown", onKeyDown);
        onCleanup(() => window.removeEventListener("keydown", onKeyDown));
    });

    return (
        <Overlay onClose={() => setRestartOpen(false)}>
            <div class="flex flex-col items-center gap-[length:var(--space-xl)] px-[length:var(--space-xl)] text-center">
                <div class="flex flex-col items-center gap-6">
                    <div class="text-[length:var(--text-h1)] font-semibold">Restart required</div>
                    <div class="text-[length:var(--text-h2)] font-medium text-muted">
                        The game must be restarted for the new display settings to apply.
                    </div>
                </div>
                <div class="flex gap-6">
                    <Button kind="success" onClick={leave}>
                        Exit to desktop
                    </Button>
                    <Button kind="danger" onClick={() => setRestartOpen(false)}>
                        Later
                    </Button>
                </div>
            </div>
        </Overlay>
    );
}
