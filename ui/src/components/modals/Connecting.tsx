import type {JSX} from "solid-js";
import {Show} from "solid-js";
import {emit} from "../../state/bridge";
import {address, download, status} from "../../state/session";
import {Button} from "../form/Button";
import {Overlay} from "./Overlay";

export function Connecting(): JSX.Element {
    const phase = () => download()?.phase ?? "";
    const determinate = () => {
        const current = download();
        return Boolean(current && (current.downloading || current.phase === "downloading") && current.progress > 0);
    };
    const percent = () => Math.round(Math.min(1, Math.max(0, download()?.progress ?? 0)) * 100);

    const title = () => {
        if (phase() === "downloading") {
            return "Downloading";
        }

        if (phase() === "authenticating") {
            return "Authenticating";
        }

        if (phase() === "starting") {
            return "Starting";
        }

        return "Connecting";
    };

    return (
        <Overlay>
            <div class="flex w-[min(28rem,90vw)] flex-col items-center gap-8 rounded-lg border-2 border-border bg-surface p-8 text-center">
                <div class="flex flex-col items-center gap-3">
                    <div class="text-[length:var(--text-h1)] font-semibold">{title()}</div>
                    <div class="text-[length:var(--text-h2)] font-medium text-muted">{address()}</div>
                </div>

                <Show when={status()}>
                    <p class="m-0 max-w-xl text-[length:var(--text-body)] leading-relaxed whitespace-pre-line text-muted">
                        {status()}
                    </p>
                </Show>

                <div class="progress w-full">
                    <div class="progress__bar">
                        <Show
                            when={determinate()}
                            fallback={<div class="progress__fill progress__fill--indeterminate" />}
                        >
                            <div class="progress__fill progress__fill--determinate" style={{width: `${percent()}%`}} />
                        </Show>
                    </div>
                    <Show when={determinate()}>
                        <div class="progress__meta">{percent()}%</div>
                    </Show>
                </div>

                <Button
                    kind="danger"
                    onClick={() => {
                        emit("connection:abort");
                    }}
                >
                    Cancel
                </Button>
            </div>
        </Overlay>
    );
}
