import type {JSX} from "solid-js";
import { createSignal, onCleanup, onMount } from "solid-js";
import { emit, on, type NetgraphSample } from "../state/bridge";

/**
 * The netgraph: frame rate, ping and throughput, in the corner.
 *
 * Always on. There is no key to toggle it and no setting to hide it - it is
 * four short rows in a corner, and something a player wants at a glance is
 * worth less if it first has to be summoned.
 *
 * WHY THE NUMBERS COME FROM THE CLIENT RATHER THAN FROM HERE
 *
 * Every value on display is something only the native side can see. The frame
 * rate is the game's, not the browser's - CEF renders the interface on its own
 * schedule, so a rate measured here would describe the overlay's compositing
 * and not what the player is actually experiencing. Ping is the transport's,
 * and the byte totals are counted at the socket.
 *
 * IT STILL ASKS TO BE FED.
 *
 * The client sends nothing until this mounts and asks. Being always visible
 * makes that ask unconditional rather than unnecessary: the client has no other
 * way to know an interface is running, and a page that never asked would show
 * four dashes forever.
 *
 * IT TAKES NO FOCUS, DELIBERATELY.
 *
 * There is nothing here to click or type into. Claiming the keyboard for a
 * read-only display would stop the player moving, with nothing on screen to
 * explain why.
 */
export function Netgraph(): JSX.Element {
    const [sample, setSample] = createSignal<NetgraphSample | undefined>();

    /**
     * Bytes per second, as the unit that suits the number.
     *
     * A player moving 40 bytes a second should not read "0.04 KB/s", and one
     * moving two megabytes should not read "2048.00 KB/s". The reference this
     * was modelled on always printed Kbps, which is unreadable at both ends of
     * that range.
     */
    function rate(bytesPerSecond: number): string {
        if (bytesPerSecond < 1024) {
            return `${Math.round(bytesPerSecond)} B/s`;
        }

        if (bytesPerSecond < 1024 * 1024) {
            return `${(bytesPerSecond / 1024).toFixed(1)} KB/s`;
        }

        return `${(bytesPerSecond / (1024 * 1024)).toFixed(2)} MB/s`;
    }

    onMount(() => {
        const unsubscribe = on("netgraph:update", (sample: NetgraphSample) => setSample(sample));

        emit("netgraph:enable", true);

        onCleanup(() => {
            unsubscribe();
            emit("netgraph:enable", false);
        });
    });

    return (
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
    );
}
