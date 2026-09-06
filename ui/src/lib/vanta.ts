/**
 * Vanta's in-page event library.
 *
 * In the client this talks to C++ over `http://vanta/emit` and
 * `http://vanta/events`. `npm run dev` uses a local stand-in.
 */

export type Handler = (...args: unknown[]) => void;

export interface VantaPage {
    on(event: string, cb: Handler): void;
    off(event: string, cb: Handler): void;
    emit(event: string, ...args: unknown[]): void;
}

declare global {
    interface Window {
        vanta?: VantaPage;
    }
}

class LocalBus implements VantaPage {
    private readonly _handlers = new Map<string, Set<Handler>>();

    on(event: string, cb: Handler): void {
        let set = this._handlers.get(event);

        if (!set) {
            set = new Set();
            this._handlers.set(event, set);
        }

        set.add(cb);
    }

    off(event: string, cb: Handler): void {
        this._handlers.get(event)?.delete(cb);
    }

    emit(event: string, ...args: unknown[]): void {
        this._handlers.get(event)?.forEach((cb) => {
            cb(...args);
        });
    }

    dispatch(event: string, args: unknown[]): void {
        this._handlers.get(event)?.forEach((cb) => {
            cb(...args);
        });
    }
}

function runningInClient(): boolean {
    return window.location.hostname === "vanta";
}

const bus = new LocalBus();

if (runningInClient()) {
    const emit = (event: string, ...args: unknown[]): void => {
        void fetch("/emit", {
            method: "POST",
            headers: {"content-type": "application/json"},
            body: JSON.stringify({event, args}),
        });
    };

    const pump = async (): Promise<void> => {
        for (;;) {
            try {
                const response = await fetch("/events", {cache: "no-store"});
                const batch = (await response.json()) as Array<{event: string; args?: unknown[]}>;

                if (batch.length === 0) {
                    await new Promise((resolve) => setTimeout(resolve, 16));
                    continue;
                }

                for (const item of batch) {
                    bus.dispatch(item.event, Array.isArray(item.args) ? item.args : []);
                }
            } catch {
                await new Promise((resolve) => setTimeout(resolve, 50));
            }
        }
    };

    void pump();

    const vanta: VantaPage = {
        on: (event, cb) => bus.on(event, cb),
        off: (event, cb) => bus.off(event, cb),
        emit,
    };

    window.vanta = vanta;
} else {
    const emit = (event: string, ...args: unknown[]): void => {
        console.info(`[vanta] ${event}`, ...args);
        bus.emit(event, ...args);
    };

    window.vanta = {
        on: (event, cb) => bus.on(event, cb),
        off: (event, cb) => bus.off(event, cb),
        emit,
    };
}

const vanta = window.vanta as VantaPage;

export default vanta;

export function inGame(): boolean {
    return runningInClient();
}

export function emit(event: string, ...args: unknown[]): void {
    vanta.emit(event, ...args);
}

export function on<Args extends unknown[]>(event: string, cb: (...args: Args) => void): () => void {
    const handler = cb as Handler;
    vanta.on(event, handler);
    return () => vanta.off(event, handler);
}
