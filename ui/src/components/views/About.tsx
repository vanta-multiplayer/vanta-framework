import type {JSX} from "solid-js";
import {buildLabel} from "../../state/session";
import {Logo, Wordmark} from "../Brand";

export function About(): JSX.Element {
    return (
        <div class="flex h-full min-h-0 flex-col">
            <header class="flex min-h-[length:var(--header-row)] items-center border-b border-border px-[length:var(--space-xl)]">
                <div class="flex items-center gap-4">
                    <Logo class="size-12" alt="" />
                    <div>
                        <Wordmark class="text-2xl" />
                        <p class="m-0 mt-1 text-xs font-semibold text-muted">{buildLabel()}</p>
                    </div>
                </div>
            </header>
            <div class="flex-1 overflow-y-auto px-[length:var(--space-xl)] py-[length:var(--space-xl)]">
                <div class="max-w-2xl">
                    <h2 class="mb-4 font-display">Modern multiplayer for San Andreas</h2>
                    <p class="mb-8 text-sm leading-7 text-muted">
                        Create servers, resources and interfaces with a modern stack without losing
                        the game that started it all. Grand Theft Auto and Grand Theft Auto: San
                        Andreas are trademarks of their respective owners. Vanta is not affiliated
                        with or endorsed by those owners. Vanta does not host player-run servers
                        and is not responsible for player-made content.
                    </p>
                    <p class="m-0 text-xs font-semibold uppercase tracking-[.16em] text-muted">
                        Esc menu · F8 console
                    </p>
                </div>
            </div>
        </div>
    );
}
