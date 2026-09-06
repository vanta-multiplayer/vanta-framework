import type {JSX} from "solid-js";
import { Show, createSignal, For, onCleanup, onMount } from "solid-js";
import { emit, on, type ConsoleLevel, type ConsoleLine } from "../state/bridge";
import { claimFocus } from "../state/focus";

/**
 * The in-game console: log output and a command line.
 *
 * Opens on F8 or backtick, the two keys players of this kind of game already
 * reach for.
 *
 * WHY IT TAKES KEYBOARD FOCUS BUT NOT THE MOUSE
 * Typing a command must not also walk the player forwards, so the keyboard has
 * to be captured. The mouse is left with the game so the player can still look
 * around while reading output - and so closing the console does not require
 * finding a cursor first.
 */
export function Console(): JSX.Element {
    const [open, setOpen] = createSignal(false);
    const [lines, setLines] = createSignal<ConsoleLine[]>([]);
    const [input, setInput] = createSignal("");
    const [history, setHistory] = createSignal<string[]>([]);
    const [historyIndex, setHistoryIndex] = createSignal(-1);

    let inputElement: HTMLInputElement | undefined;
    let outputElement: HTMLDivElement | undefined;

    /**
     * The time, formatted the way the native logger writes it.
     *
     * NOT toLocaleTimeString: on most locales that gives a 12-hour clock, so an
     * echoed command read "5:45:27 AM" beside log lines reading "05:45:27" and
     * the column stopped lining up.
     */
    function timestamp(): string {
        const now = new Date();
        const pad = (value: number) => value.toString().padStart(2, "0");

        return `${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`;
    }

    /** Keeps the newest line visible. */
    function scrollToBottom(): void {
        queueMicrotask(() => {
            if (outputElement === undefined) {
                return;
            }

            outputElement.scrollTop = outputElement.scrollHeight;
        });
    }

    function append(line: ConsoleLine): void {
        setLines((current) => {
            const next = [...current, line];

            // Bounded: a chatty resource would otherwise grow this without limit
            // over a session that may last hours.
            return next.length > 500 ? next.slice(-500) : next;
        });

        scrollToBottom();
    }

    function toggle(): void {
        const next = !open();
        setOpen(next);

        // Keyboard only: the console does not need the mouse, and taking it
        // would hide the crosshair for no reason.
        //
        // Claimed rather than set, so closing the console does not also release
        // the MENU's focus if the menu happens to be open.
        claimFocus("console", next, false);

        emit("console:setState", next);

        if (next) {
            // Off-screen CEF often ignores element.focus() until a click.
            // The window handler below still types if this does nothing.
            queueMicrotask(() => inputElement?.focus());
            requestAnimationFrame(() => inputElement?.focus());
        }
    }

    function runCommand(command: string): void {
        const trimmed = command.trim();

        if (trimmed === "") {
            return;
        }

        append({
            level: "info",
            category: "Console",
            message: `> ${trimmed}`,
            timestamp: timestamp(),
        });

        // clear is handled here rather than natively: the page owns what is on
        // screen, and a round trip to the client only to come back and empty a
        // list would be indirection for its own sake.
        const word = trimmed.toLowerCase();

        if (word === "clear" || word === "cls") {
            setLines([]);
        } else {
            emit("console:execute", trimmed);
        }

        setHistory((current) => [trimmed, ...current.filter((entry) => entry !== trimmed)].slice(0, 50));
        setHistoryIndex(-1);
        setInput("");
    }

    function submit(event: SubmitEvent): void {
        event.preventDefault();
        runCommand(input());
    }

    /** Up and down walk the command history, as a shell does. */
    function onInputKeyDown(event: KeyboardEvent): void {
        if (event.key === "Enter") {
            event.preventDefault();
            runCommand(input());
            return;
        }

        if (event.key === "ArrowUp") {
            event.preventDefault();

            const next = Math.min(historyIndex() + 1, history().length - 1);

            if (next >= 0) {
                setHistoryIndex(next);
                setInput(history()[next] ?? "");
            }

            return;
        }

        if (event.key === "ArrowDown") {
            event.preventDefault();

            const next = historyIndex() - 1;
            setHistoryIndex(next);
            setInput(next < 0 ? "" : (history()[next] ?? ""));
        }
    }

    onMount(() => {
        function onKeyDown(event: KeyboardEvent): void {
            // F8 or backtick toggles. Backquote is matched by code rather than
            // key so it works on layouts where the character differs.
            if (event.key === "F8" || event.code === "Backquote") {
                event.preventDefault();
                toggle();
                return;
            }

            if (event.key === "Escape" && open()) {
                event.preventDefault();
                toggle();
                return;
            }

            if (!open()) {
                return;
            }

            // Letters come from CEF's WM_CHAR into the field. Filling the
            // signal here as well wrote every character twice. Enter still
            // has to be taken when the field has no DOM focus: Framework
            // drops WM_CHAR VK_RETURN, so the form never submits on its own.
            if (document.activeElement === inputElement) {
                return;
            }

            if (event.key === "Enter") {
                event.preventDefault();
                runCommand(input());
                return;
            }

            if (event.key === "ArrowUp" || event.key === "ArrowDown") {
                onInputKeyDown(event);
            }
        }

        window.addEventListener("keydown", onKeyDown);

        const stopLog = on("console:push", (...args: unknown[]) => {
            const first = args[0];

            if (first && typeof first === "object" && "message" in first) {
                const line = first as Partial<ConsoleLine>;
                append({
                    level: line.level ?? "info",
                    category: line.category ?? "",
                    message: String(line.message),
                    timestamp: line.timestamp || timestamp(),
                });
                return;
            }

            if (typeof first === "number" && typeof args[1] === "string") {
                const level: ConsoleLevel = first >= 12 ? "error" : first >= 8 ? "warn" : "info";
                append({
                    level,
                    category: "",
                    message: args[1],
                    timestamp: timestamp(),
                });
            }
        });

        const stopClear = on("console:clear", () => setLines([]));

        const stopOpen = on("console:open", (state?: boolean) => {
            if (state !== undefined && state !== open()) {
                toggle();
            }
        });

        onCleanup(() => {
            window.removeEventListener("keydown", onKeyDown);
            stopLog();
            stopClear();
            stopOpen();
        });
    });

    /**
     * Severity tints the whole row, not just the text.
     *
     * A wall of log output is scanned before it is read, and the eye finds a
     * coloured band far faster than it finds a coloured word.
     */
    function lineClasses(level: ConsoleLevel): string {
        switch (level) {
            case "fatal":
                // Fatal is inverted rather than tinted: it is the one severity
                // that should stop a reader, and a stronger red would not be
                // distinguishable from an ordinary error at a glance.
                return "bg-level-error/25 text-level-error font-medium";
            case "error":
                return "bg-level-error/10 text-level-error";
            case "warn":
                return "bg-level-warn/10 text-level-warn";
            case "debug":
                return "text-text-faint";
            default:
                return "text-text-light";
        }
    }

    /**
     * The category badge, coloured to match the row.
     *
     * The row itself uses items-start rather than the default stretch: a flex
     * row stretches its children to the tallest one, so a message that wrapped
     * to two lines made the badge grow to match and turn into a tall rectangle.
     *
     * The badge was a fixed slate colour regardless of severity, so an error
     * line had a red message beside a neutral label - which read as though the
     * two were unrelated.
     */
    function badgeClasses(level: ConsoleLevel): string {
        switch (level) {
            case "fatal":
            case "error":
                return "border-level-error/40 bg-level-error/20 text-level-error";
            case "warn":
                return "border-level-warn/40 bg-level-warn/20 text-level-warn";
            case "debug":
                return "border-border bg-surface text-muted";
            default:
                return "border-border bg-surface-elevated text-muted";
        }
    }

    return (
        <div
            // Slid out of view rather than unmounted, so reopening keeps the
            // scroll position and the output does not have to be rebuilt.
            class="fixed inset-x-0 top-0 z-[100] flex h-[45vh] flex-col border-b border-border
                   bg-background/88 transition-transform duration-150 ease-out"
            classList={{
                "translate-y-0 pointer-events-auto": open(),
                "-translate-y-full pointer-events-none": !open(),
            }}
            aria-hidden={!open()}
        >
            <div
                ref={outputElement}
                class="text-console flex-1 overflow-y-auto py-2 font-mono leading-relaxed select-text"
            >
                <For each={lines()}>
                    {(line) => (
                        <div class={`flex items-start gap-3 px-5 py-1 wrap-break-word whitespace-pre-wrap ${lineClasses(line.level)}`}>
                            <span class="shrink-0 text-muted tabular-nums">{line.timestamp}</span>

                            <Show when={line.category}>
                                <span class={`shrink-0 rounded border px-1.5 ${badgeClasses(line.level)}`}>
                                    {line.category}
                                </span>
                            </Show>

                            <span class="min-w-0 flex-1">{line.message}</span>
                        </div>
                    )}
                </For>
            </div>

            <form class="flex items-center gap-2 border-t border-border bg-brown-dark/50 px-5 py-2" onSubmit={submit}>
                <span class="font-mono text-primary">&gt;</span>

                <input
                    ref={inputElement}
                    class="text-console flex-1 border-0 bg-transparent font-mono text-inherit
                           outline-none select-text placeholder:text-muted/40"
                    type="text"
                    value={input()}
                    spellcheck={false}
                    autocomplete="off"
                    autofocus
                    placeholder="Type a command, or 'help'"
                    onInput={(event) => setInput(event.currentTarget.value)}
                    onKeyDown={onInputKeyDown}
                />
            </form>
        </div>
    );
}
