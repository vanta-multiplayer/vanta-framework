import type {JSX} from "solid-js";
import {onCleanup, onMount} from "solid-js";
import {emit} from "../../state/bridge";
import {setExitOpen} from "../../state/session";
import {Button} from "../form/Button";
import {Overlay} from "./Overlay";

export function ExitModal(): JSX.Element {
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
                setExitOpen(false);
            }
        }

        window.addEventListener("keydown", onKeyDown);
        onCleanup(() => window.removeEventListener("keydown", onKeyDown));
    });

    return (
        <Overlay onClose={() => setExitOpen(false)}>
            <div class="flex flex-col items-center gap-8 text-center">
                <div>
                    <div class="font-display text-3xl font-bold tracking-tight">Leave Vanta?</div>
                    <div class="mt-3 text-sm font-semibold text-muted">This closes the game.</div>
                </div>
                <div class="flex gap-3">
                    <Button variant="danger" onClick={leave}>
                        Exit to desktop
                    </Button>
                    <Button variant="secondary" onClick={() => setExitOpen(false)}>
                        Cancel
                    </Button>
                </div>
            </div>
        </Overlay>
    );
}
