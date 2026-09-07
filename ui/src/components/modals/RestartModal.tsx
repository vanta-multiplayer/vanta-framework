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
            <div class="flex flex-col items-center gap-8 text-center">
                <div>
                    <div class="font-display text-3xl font-bold tracking-tight">Restart required</div>
                    <div class="mt-3 text-sm font-semibold leading-relaxed text-muted">
                        The game must be restarted for the new display settings to apply.
                    </div>
                </div>
                <div class="flex gap-3">
                    <Button variant="primary" onClick={leave}>
                        Exit to desktop
                    </Button>
                    <Button variant="secondary" onClick={() => setRestartOpen(false)}>
                        Later
                    </Button>
                </div>
            </div>
        </Overlay>
    );
}
