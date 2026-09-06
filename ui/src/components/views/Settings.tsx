import type {JSX} from "solid-js";
import {Show} from "solid-js";
import {Field} from "../form/Field";
import {DisplaySettings} from "../DisplaySettings";
import {connecting, nickname, persistNickname, setNickname, settings} from "../../state/session";

export function Settings(): JSX.Element {
    const display = () => {
        const data = settings();

        if (!data) {
            return null;
        }

        return {
            modes: [...(data.modes ?? [])],
            displays: [...(data.displays ?? [])],
            windowWidth: data.windowWidth ?? 1280,
            windowHeight: data.windowHeight ?? 720,
            fullscreen: data.fullscreen ?? false,
            display: data.display ?? 0,
        };
    };

    return (
        <div class="flex h-full min-h-0">
            <div class="flex w-[length:var(--settings-rail)] shrink-0 flex-col border-r border-border">
                <div class="flex min-h-[length:var(--header-row)] items-center border-b border-border px-[length:var(--space-xl)]">
                    <h1>Settings</h1>
                </div>
                <div class="flex-1 overflow-y-auto px-[length:var(--space-xl)] py-[length:var(--space-xl)]">
                    <div class="flex flex-col gap-8">
                        <Field
                            id="settings-nickname"
                            label="Name"
                            value={nickname()}
                            maxLength={24}
                            disabled={connecting()}
                            onInput={setNickname}
                            onBlur={() => persistNickname(nickname())}
                        />
                        <Show when={display()}>
                            {(data) => <DisplaySettings {...data()} />}
                        </Show>
                    </div>
                </div>
            </div>
            <div class="min-w-0 flex-1" />
        </div>
    );
}
