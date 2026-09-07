import type {JSX} from "solid-js";
import {Show, createMemo, onCleanup, onMount} from "solid-js";
import {emit} from "../state/bridge";
import {connected, micLevel, micTest, setMicTest, showMenu, voiceDevices} from "../state/session";
import {Dropdown, type DropdownOption} from "./Dropdown";
import {Button} from "./form/Button";
import {KeyInput} from "./form/KeyInput";
import {Slider} from "./form/Slider";
import {VolumeMeter} from "./form/VolumeMeter";

export interface VoiceSettingsProps {
    voiceInputDevice: string;
    voiceOutputDevice: string;
    voiceVolume: number;
    voicePttKey: number;
}

type VoiceDevice = {
    id: string;
    name: string;
    isDefault?: boolean;
};

function optionsFrom(devices: VoiceDevice[], fallback: string): DropdownOption[] {
    const defaultDevice = devices.find((device) => device.isDefault);

    return [
        {value: "default", label: defaultDevice ? `Default — ${defaultDevice.name}` : fallback},
        ...devices.map((device) => ({value: device.id, label: device.name})),
    ];
}

export function VoiceSettings(props: VoiceSettingsProps): JSX.Element {
    const inputOptions = createMemo(() => optionsFrom(voiceDevices().input, "Default input"));
    const outputOptions = createMemo(() => optionsFrom(voiceDevices().output, "Default output"));

    onMount(() => {
        emit("settings:devices:reload");
    });

    onCleanup(() => {
        if (micTest()) {
            setMicTest(false);
            emit("settings:currentVolume:toggle", false);
        }
    });

    function toggleTest(): void {
        emit("settings:currentVolume:toggle", !micTest());
    }

    function startTalk(): void {
        if (micTest()) {
            setMicTest(false);
            emit("settings:currentVolume:toggle", false);
        }

        emit("settings:talk:toggle", true);
        showMenu(false);
    }

    return (
        <div class="flex flex-col gap-8">
            <div>
                <h3 class="mb-4">Input device</h3>
                <Dropdown
                    options={inputOptions()}
                    value={props.voiceInputDevice}
                    unknownLabel="Unknown input"
                    onChange={(value) => emit("settings:change", "voiceInputDevice", value)}
                />
                <p class="mt-2 mb-0 text-[length:var(--text-label)] text-muted">
                    Voice chat and the mic test both use this device.
                </p>
            </div>

            <div>
                <h3 class="mb-4">Output device</h3>
                <Dropdown
                    options={outputOptions()}
                    value={props.voiceOutputDevice}
                    unknownLabel="Unknown output"
                    onChange={(value) => emit("settings:change", "voiceOutputDevice", value)}
                />
                <p class="mt-2 mb-0 text-[length:var(--text-label)] text-muted">
                    Voice switches now. Game audio uses this device on the next launch.
                </p>
            </div>

            <VolumeMeter label={micTest() ? "Mic test" : connected() ? "Talking" : "Mic test"} value={micLevel()} />

            <Button kind={micTest() ? "danger" : "ghost"} onClick={toggleTest}>
                {micTest() ? "Stop mic test" : "Start mic test"}
            </Button>
            <p class="mt-0 mb-0 text-[length:var(--text-label)] text-muted">
                Hear yourself on the output device. Voice chat pauses while this runs.
            </p>

            <Show when={connected()}>
                <Button kind="primary" onClick={startTalk}>
                    Talk
                </Button>
                <p class="mt-0 mb-0 text-[length:var(--text-label)] text-muted">
                    Closes the menu and transmits until you open it again. Stay near the other player.
                </p>
            </Show>

            <Slider
                id="voice-volume"
                label="Voice output"
                value={Math.round(props.voiceVolume * 100)}
                max={200}
                onChange={(value) => emit("settings:change", "voiceVolume", value / 100)}
            />

            <div>
                <h3 class="mb-4">In-game key</h3>
                <KeyInput
                    value={props.voicePttKey}
                    onChange={(value) => emit("settings:change", "voicePttKey", value)}
                />
                <p class="mt-2 mb-0 text-[length:var(--text-label)] text-muted">
                    Hold this key in the world, with the menu closed. It does not talk by clicking it.
                </p>
            </div>
        </div>
    );
}
