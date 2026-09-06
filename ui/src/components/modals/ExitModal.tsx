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
            <div class="flex flex-col items-center gap-[length:var(--space-xl)] px-[length:var(--space-xl)] text-center">
                <div class="flex flex-col items-center gap-6">
                    <div class="text-[length:var(--text-h1)] font-semibold">Confirm</div>
                    <div class="text-[length:var(--text-h2)] font-medium text-muted">
                        Exit to desktop?
                    </div>
                </div>
                <div class="flex gap-6">
                    <Button kind="success" onClick={leave}>
                        Exit to desktop
                    </Button>
                    <Button kind="danger" onClick={() => setExitOpen(false)}>
                        Cancel
                    </Button>
                </div>
            </div>
        </Overlay>
    );
}
