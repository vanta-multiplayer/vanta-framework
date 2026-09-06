import type {JSX} from "solid-js";
import { createMemo, createSignal, Show } from "solid-js";
import { emit } from "../state/bridge";
import { Dropdown, type DropdownOption } from "./Dropdown";
import { Button } from "./form/Button";

/** One resolution the graphics device reports. */
export interface VideoModeEntry {
    width: number;
    height: number;
    label: string;
}

/** One monitor the game could open on. */
export interface DisplayEntry {
    index: number;
    width: number;
    height: number;
    primary: boolean;
    name: string;
}

export interface DisplaySettingsProps {
    modes: VideoModeEntry[];
    displays: DisplayEntry[];
    windowWidth: number;
    windowHeight: number;
    fullscreen: boolean;
    display: number;
}

/**
 * Resolution, borderless and monitor selection.
 *
 * WHY THE CHANGES SAY "ON NEXT LAUNCH"
 * The graphics device is created once during the game's own start-up, and
 * switching it live means resetting D3D9 while CEF's textures are live on it.
 * Telling the player plainly is better than a control that looks like it
 * worked and did not.
 */
export function DisplaySettings(props: DisplaySettingsProps): JSX.Element {
    const [width, setWidth] = createSignal(props.windowWidth);
    const [height, setHeight] = createSignal(props.windowHeight);
    const [fullscreen, setFullscreen] = createSignal(props.fullscreen);
    const [display, setDisplay] = createSignal(props.display);
    const [saved, setSaved] = createSignal(false);

    function save(): void {
        emit("settings:change", "windowWidth", width());
        emit("settings:change", "windowHeight", height());
        emit("settings:change", "fullscreen", fullscreen());
        emit("settings:change", "display", display());

        setSaved(true);
    }

    const modeOptions = createMemo<DropdownOption[]>(() => [
        { value: "0x0", label: "Match the display" },
        ...props.modes.map((mode) => ({
            value: `${mode.width}x${mode.height}`,
            label: mode.label,
        })),
    ]);

    const displayOptions = createMemo<DropdownOption[]>(() =>
        props.displays.map((entry) => ({
            value: String(entry.index),
            label: `${entry.name} — ${entry.width}×${entry.height}${
                entry.primary ? " (primary)" : ""
            }`,
        })),
    );

    function chooseMode(value: string): void {
        const [w, h] = value.split("x").map((part) => Number.parseInt(part, 10));

        setWidth(Number.isFinite(w) ? w : 0);
        setHeight(Number.isFinite(h) ? h : 0);
        setSaved(false);
    }

    return (
        <div class="flex flex-col gap-8">
            <div>
                <h3 class="mb-4">Resolution</h3>
                <Dropdown
                    options={modeOptions()}
                    value={`${width()}x${height()}`}
                    disabled={fullscreen()}
                    onChange={chooseMode}
                />
                <Show when={fullscreen()}>
                    <p class="mt-2 mb-0 text-[length:var(--text-label)] text-muted">
                        Borderless always fills the selected display.
                    </p>
                </Show>
            </div>

            <Show when={props.displays.length > 1}>
                <div>
                    <h3 class="mb-4">Monitor</h3>
                    <Dropdown
                        options={displayOptions()}
                        value={String(display())}
                        onChange={(value) => {
                            setDisplay(Number.parseInt(value, 10) || 0);
                            setSaved(false);
                        }}
                    />
                </div>
            </Show>

            <label class="flex cursor-pointer items-center gap-3 text-[length:var(--text-body)]">
                <input
                    type="checkbox"
                    class="size-4 accent-primary"
                    checked={fullscreen()}
                    onChange={(event) => {
                        setFullscreen(event.currentTarget.checked);
                        setSaved(false);
                    }}
                />
                Borderless fullscreen
            </label>

            <div class="flex items-center gap-4">
                <Button kind="primary" onClick={save}>
                    Save
                </Button>
                <Show when={saved()}>
                    <span class="text-[length:var(--text-label)] text-success">
                        Saved — applied the next time Vanta starts.
                    </span>
                </Show>
            </div>
        </div>
    );
}
