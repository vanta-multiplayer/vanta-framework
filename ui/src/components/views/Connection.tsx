import type {JSX} from "solid-js";
import {Show} from "solid-js";
import {emit} from "../../state/bridge";
import {useNavigate} from "@solidjs/router";
import {routes} from "../../routes";
import {address, connecting, download, status} from "../../state/session";
import {Button} from "../form/Button";

export function Connection(): JSX.Element {
    const navigate = useNavigate();

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

        if (connecting()) {
            return "Connecting";
        }

        const message = status().toLowerCase();

        if (message.includes("fail") || message.includes("error") || message.includes("timeout")) {
            return "Connection failed";
        }

        if (message.includes("disconnect")) {
            return "Disconnected";
        }

        if (message.includes("connected")) {
            return "Connected";
        }

        return status() || "Connection";
    };

    return (
        <div class="flex h-full flex-col items-center justify-center gap-[length:var(--space-xl)] px-[length:var(--space-xl)] text-center">
            <div class="flex flex-col items-center gap-8">
                <div class="text-[length:var(--text-h1)] font-semibold">{title()}</div>
                <div class="text-[length:var(--text-h2)] font-medium text-muted">{address()}</div>
            </div>

            <Show when={status()}>
                <p class="m-0 max-w-xl text-[length:var(--text-body)] leading-relaxed whitespace-pre-line">
                    {status()}
                </p>
            </Show>

            <Show when={connecting()}>
                <div class="progress">
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
            </Show>

            <div class="flex gap-4">
                <Show when={connecting()}>
                    <Button
                        kind="danger"
                        onClick={() => {
                            emit("connection:abort");
                        }}
                    >
                        Cancel
                    </Button>
                </Show>
                <Show when={!connecting()}>
                    <Button kind="ghost" onClick={() => navigate(routes.home)}>
                        Back
                    </Button>
                </Show>
            </div>
        </div>
    );
}
