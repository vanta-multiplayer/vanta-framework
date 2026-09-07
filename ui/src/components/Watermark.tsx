import type {JSX} from "solid-js";
import {Show} from "solid-js";
import {Logo} from "./Brand";
import {buildLabel, connected, menuOpen} from "../state/session";

export function Watermark(): JSX.Element {
    return (
        <Show when={connected() && !menuOpen()}>
            <div class="watermark" aria-hidden="true">
                <Logo class="watermark__mark" alt="" />
                <div>
                    <div class="watermark__name">vanta.mp</div>
                    <div class="watermark__meta">{buildLabel()}</div>
                </div>
            </div>
        </Show>
    );
}
