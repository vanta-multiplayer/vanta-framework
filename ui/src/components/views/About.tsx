import type {JSX} from "solid-js";
import {version} from "../../state/session";

export function About(): JSX.Element {
    return (
        <div class="flex h-full min-h-0 flex-col">
            <header class="flex min-h-[length:var(--header-row)] items-center border-b border-border px-[length:var(--space-xl)]">
                <h1>Vanta {version()}</h1>
            </header>
            <div class="flex-1 overflow-y-auto px-[length:var(--space-xl)] py-[length:var(--space-xl)]">
                <div class="max-w-2xl">
                    <h2 class="mb-4">Multiplayer for GTA: San Andreas</h2>
                    <p class="mb-8 text-[length:var(--text-body)] leading-relaxed text-muted">
                        Grand Theft Auto and Grand Theft Auto: San Andreas are trademarks of their
                        respective owners. Vanta is not affiliated with or endorsed by those
                        owners. Vanta does not host player-run servers and is not responsible for
                        player-made content.
                    </p>
                    <p class="m-0 text-[length:var(--text-label)] text-accent-muted">Esc / F1 menu · F8 console</p>
                </div>
            </div>
        </div>
    );
}
