import type {JSX} from "solid-js";
import {Show, createEffect, createSignal, onCleanup, onMount} from "solid-js";
import {emit, on, type NetgraphSample} from "../state/bridge";
import {connected, netgraphEnabled} from "../state/session";

export function Netgraph(): JSX.Element {
    const [sample, setSample] = createSignal<NetgraphSample | undefined>();

    function rate(bytesPerSecond: number): string {
        if (bytesPerSecond < 1024) {
            return `${Math.round(bytesPerSecond)} B/s`;
        }

        if (bytesPerSecond < 1024 * 1024) {
            return `${(bytesPerSecond / 1024).toFixed(1)} KB/s`;
        }

        return `${(bytesPerSecond / (1024 * 1024)).toFixed(2)} MB/s`;
    }

    const visible = () => netgraphEnabled() && connected();

    createEffect(() => {
        emit("netgraph:enable", visible());
    });

    onMount(() => {
        const unsubscribe = on("netgraph:update", (next: NetgraphSample) => setSample(next));

        onCleanup(() => {
            unsubscribe();
            emit("netgraph:enable", false);
        });
    });

    return (
        <Show when={visible()}>
            <table class="netgraph">
                <tbody>
                    <tr>
                        <td>FPS:</td>
                        <td>{sample()?.fps ?? "--"}</td>
                    </tr>
                    <tr>
                        <td>PING:</td>
                        <td>{sample()?.connected === true ? `${sample()?.rtt} ms` : "--"}</td>
                    </tr>
                    <tr>
                        <td>OUT:</td>
                        <td>{sample() !== undefined ? rate(sample()!.tx) : "--"}</td>
                    </tr>
                    <tr>
                        <td>IN:</td>
                        <td>{sample() !== undefined ? rate(sample()!.rx) : "--"}</td>
                    </tr>
                </tbody>
            </table>
        </Show>
    );
}
