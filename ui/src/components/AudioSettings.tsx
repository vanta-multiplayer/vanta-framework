import type {JSX} from "solid-js";
import {emit} from "../state/bridge";
import {Slider} from "./form/Slider";

export interface AudioSettingsProps {
    volumeMaster: number;
    volumeSfx: number;
    volumeRadio: number;
}

function asPercent(value: number): number {
    return Math.round(Math.min(1, Math.max(0, value)) * 100);
}

/**
 * Master, SFX and radio. Values come from the live settings snapshot
 * so leaving the page and coming back shows what the engine is using.
 */
export function AudioSettings(props: AudioSettingsProps): JSX.Element {
    function push(key: "volumeMaster" | "volumeSfx" | "volumeRadio", value: number): void {
        emit("settings:change", key, value / 100);
    }

    return (
        <div class="flex flex-col gap-8">
            <h3 class="mb-0">Game audio</h3>
            <Slider
                id="volume-master"
                label="Master"
                value={asPercent(props.volumeMaster)}
                onChange={(value) => push("volumeMaster", value)}
            />
            <Slider
                id="volume-sfx"
                label="SFX"
                value={asPercent(props.volumeSfx)}
                onChange={(value) => push("volumeSfx", value)}
            />
            <Slider
                id="volume-radio"
                label="Radio"
                value={asPercent(props.volumeRadio)}
                onChange={(value) => push("volumeRadio", value)}
            />
        </div>
    );
}
