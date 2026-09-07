import type {JSX} from "solid-js";
import { createMemo, createSignal, Show } from "solid-js";
import { emit } from "../state/bridge";
import { notifyDisplaySaved } from "../state/session";
import { Dropdown, type DropdownOption } from "./Dropdown";
import { Button } from "./form/Button";
import { Checkbox } from "./form/Checkbox";

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
    borderless: boolean;
    exclusive: boolean;
    display: number;
}

/**
 * Resolution, borderless, exclusive fullscreen and monitor selection.
 *
 * Saved on Save. A live device Reset on this exe crashes, so a change
 * writes VantaMP.json and asks the player to restart.
 */
export function DisplaySettings(props: DisplaySettingsProps): JSX.Element {
    const [width, setWidth] = createSignal(props.windowWidth);
    const [height, setHeight] = createSignal(props.windowHeight);
    const [borderless, setBorderless] = createSignal(props.borderless);
    const [exclusive, setExclusive] = createSignal(props.exclusive);
    const [display, setDisplay] = createSignal(props.display);
    const [saved, setSaved] = createSignal(false);

    function save(): void {
        emit("settings:change", "windowWidth", width());
        emit("settings:change", "windowHeight", height());
        emit("settings:change", "borderless", borderless());
        emit("settings:change", "exclusive", exclusive());
        emit("settings:change", "display", display());

        setSaved(true);
        notifyDisplaySaved({
            windowWidth: width(),
            windowHeight: height(),
            borderless: borderless(),
            exclusive: exclusive(),
        });
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
                    disabled={borderless() && !exclusive()}
                    onChange={chooseMode}
                />
                <Show when={borderless() && !exclusive()}>
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

            <Checkbox
                label="Borderless window"
                checked={borderless() && !exclusive()}
                onChange={(on) => {
                    setBorderless(on);
                    if (on) {
                        setExclusive(false);
                    }
                    setSaved(false);
                }}
            />

            <Checkbox
                label="Exclusive fullscreen"
                checked={exclusive()}
                onChange={(on) => {
                    setExclusive(on);
                    if (on) {
                        setBorderless(false);
                    }
                    setSaved(false);
                }}
            />

            <div class="flex items-center gap-4">
                <Button kind="primary" onClick={save}>
                    Save
                </Button>
                <Show when={saved()}>
                    <span class="text-[length:var(--text-label)] text-success">
                        Saved.
                    </span>
                </Show>
            </div>
        </div>
    );
}
