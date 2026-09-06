import type {JSX} from "solid-js";
import {routes} from "../../routes";
import {address, disconnect, goTo, setExitOpen, showMenu} from "../../state/session";

function Action(props: {label: string; hint?: string; danger?: boolean; onClick: () => void}): JSX.Element {
    return (
        <button type="button" class="pause-item" classList={{"pause-item--danger": props.danger}} onClick={props.onClick}>
            <span class="pause-item__mark" />
            <span>{props.label}</span>
            <span class="pause-item__hint">{props.hint ?? ""}</span>
        </button>
    );
}

export function Pause(): JSX.Element {
    return (
        <div class="pause">
            <p class="pause__eyebrow">Paused</p>
            <h1 class="pause__title">In session</h1>
            <p class="pause__server">{address() || "Connected"}</p>

            <div class="pause__actions">
                <Action label="Resume" hint="Esc" onClick={() => showMenu(false)} />
                <Action label="Disconnect" danger onClick={disconnect} />
                <Action label="Settings" onClick={() => goTo(routes.settings)} />
                <Action label="Exit to desktop" onClick={() => setExitOpen(true)} />
            </div>
        </div>
    );
}
