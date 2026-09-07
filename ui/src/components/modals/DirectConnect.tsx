import type {JSX} from "solid-js";
import {createSignal} from "solid-js";
import {address, connectTo, setAddress, setDirectOpen} from "../../state/session";
import {Button} from "../form/Button";
import {Field} from "../form/Field";
import {Overlay} from "./Overlay";

export function DirectConnect(): JSX.Element {
    const [password, setPassword] = createSignal("");

    function go(): void {
        connectTo(address(), password());
    }

    return (
        <Overlay onClose={() => setDirectOpen(false)}>
            <h2 class="mb-6 text-center">Direct connect</h2>
            <div class="flex flex-col gap-4">
                <Field
                    id="direct-address"
                    value={address()}
                    placeholder="Server address"
                    onInput={setAddress}
                    onEnter={go}
                />
                <div class="flex items-end gap-3">
                    <div class="min-w-0 flex-1">
                        <Field
                            id="direct-password"
                            type="password"
                            value={password()}
                            placeholder="Password, if needed"
                            onInput={setPassword}
                            onEnter={go}
                        />
                    </div>
                    <Button variant="primary" onClick={go}>
                        Connect
                    </Button>
                </div>
            </div>
        </Overlay>
    );
}
