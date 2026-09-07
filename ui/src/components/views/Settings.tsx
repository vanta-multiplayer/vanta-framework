import type {JSX} from "solid-js";
import {Show} from "solid-js";
import {AudioSettings} from "../AudioSettings";
import {DisplaySettings} from "../DisplaySettings";
import {VoiceSettings} from "../VoiceSettings";
import {Field} from "../form/Field";
import {connecting, nickname, persistNickname, setNickname, settings} from "../../state/session";

export function Settings(): JSX.Element {
    const data = () => settings();

    const display = () => {
        const current = data();

        if (!current) {
            return null;
        }

        return {
            modes: [...(current.modes ?? [])],
            displays: [...(current.displays ?? [])],
            windowWidth: current.windowWidth ?? 1280,
            windowHeight: current.windowHeight ?? 720,
            borderless: current.borderless ?? current.fullscreen ?? false,
            exclusive: current.exclusive ?? false,
            display: current.display ?? 0,
        };
    };

    const audio = () => {
        const current = data();

        if (!current) {
            return null;
        }

        return {
            volumeMaster: current.volumeMaster ?? 1,
            volumeSfx: current.volumeSfx ?? 1,
            volumeRadio: current.volumeRadio ?? 1,
        };
    };

    const voice = () => {
        const current = data();

        if (!current) {
            return null;
        }

        return {
            voiceInputDevice: current.voiceInputDevice ?? "default",
            voiceOutputDevice: current.voiceOutputDevice ?? "default",
            voiceVolume: current.voiceVolume ?? 1,
            voicePttKey: current.voicePttKey ?? 0x56,
        };
    };

    return (
        <div class="flex h-full min-h-0">
            <div class="flex w-[length:var(--settings-rail)] shrink-0 flex-col border-r border-border">
                <div class="flex min-h-[length:var(--header-row)] items-center border-b border-border px-[length:var(--space-xl)]">
                    <h1 class="text-2xl">Settings</h1>
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
                        <Show when={audio()}>
                            {(values) => <AudioSettings {...values()} />}
                        </Show>
                        <Show when={display()}>
                            {(values) => <DisplaySettings {...values()} />}
                        </Show>
                    </div>
                </div>
            </div>
            <div class="flex w-[length:var(--voice-rail)] shrink-0 flex-col border-r border-border">
                <div class="flex min-h-[length:var(--header-row)] items-center border-b border-border px-[length:var(--space-xl)]">
                    <h1 class="text-2xl">Voice</h1>
                </div>
                <div class="flex-1 overflow-y-auto px-[length:var(--space-xl)] py-[length:var(--space-xl)]">
                    <Show when={voice()}>
                        {(values) => <VoiceSettings {...values()} />}
                    </Show>
                </div>
            </div>
            <div class="min-w-0 flex-1" />
        </div>
    );
}
