import type {JSX} from "solid-js";
import {Show, onCleanup, onMount} from "solid-js";
import {HashRouter, Route, useLocation, useNavigate} from "@solidjs/router";
import {Transition} from "solid-transition-group";
import {Console} from "./Console";
import {Netgraph} from "./Netgraph";
import {SideNav} from "./SideNav";
import {ConnectionError} from "./modals/ConnectionError";
import {DirectConnect} from "./modals/DirectConnect";
import {ExitModal} from "./modals/ExitModal";
import {About} from "./views/About";
import {Connection} from "./views/Connection";
import {Home} from "./views/Home";
import {Pause} from "./views/Pause";
import {Servers} from "./views/Servers";
import {Settings} from "./views/Settings";
import {IconMark} from "./icons";
import {routes} from "../routes";
import {inGame} from "../state/bridge";
import {
    bindNavigate,
    bindSession,
    connected,
    directOpen,
    errorOpen,
    exitOpen,
    handleEscape,
    menuOpen,
    ready,
    showMenu,
} from "../state/session";

function Shell(props: {children?: JSX.Element}): JSX.Element {
    const navigate = useNavigate();
    const location = useLocation();

    onMount(() => {
        bindNavigate(navigate);
        const stop = bindSession();

        function onKeyDown(event: KeyboardEvent): void {
            // In the client these keys are taken by the window hook so they
            // still work after the menu closes (CEF then has no focus).
            if (inGame()) {
                return;
            }

            if (event.key === "F1") {
                event.preventDefault();
                showMenu(!menuOpen());
                return;
            }

            if (event.key === "Escape") {
                event.preventDefault();
                handleEscape();
            }
        }

        window.addEventListener("keydown", onKeyDown);

        onCleanup(() => {
            window.removeEventListener("keydown", onKeyDown);
            stop();
        });
    });

    return (
        <>
            <Transition name="fade">
                <Show when={!ready()}>
                    <div class="splash">
                        <IconMark class="splash__mark" />
                        <div class="progress">
                            <div class="progress__bar">
                                <div class="progress__fill progress__fill--indeterminate" />
                            </div>
                        </div>
                    </div>
                </Show>
            </Transition>

            <Transition name="fade">
                <Show when={ready() && menuOpen()}>
                    <div
                        class="page"
                        classList={{
                            "page--in-world": connected(),
                            "page--pause": connected() && location.pathname === routes.pause,
                        }}
                    >
                        <Show when={!connected() || location.pathname !== routes.pause}>
                            <SideNav />
                        </Show>
                        <main class="min-h-0 min-w-0 overflow-hidden">
                            <Transition name="page" mode="outin">
                                <Show when={location.pathname} keyed>
                                    <div class="page-view">{props.children}</div>
                                </Show>
                            </Transition>
                        </main>
                    </div>
                </Show>
            </Transition>

            <Transition name="fade">
                <Show when={directOpen()}>
                    <DirectConnect />
                </Show>
            </Transition>
            <Transition name="fade">
                <Show when={exitOpen()}>
                    <ExitModal />
                </Show>
            </Transition>
            <Transition name="fade">
                <Show when={errorOpen()}>
                    <ConnectionError />
                </Show>
            </Transition>

            <Console />
            <Netgraph />
        </>
    );
}

export function App(): JSX.Element {
    return (
        <HashRouter root={Shell}>
            <Route path={routes.home} component={Home} />
            <Route path={routes.servers} component={Servers} />
            <Route path={routes.settings} component={Settings} />
            <Route path={routes.about} component={About} />
            <Route path={routes.connection} component={Connection} />
            <Route path={routes.pause} component={Pause} />
        </HashRouter>
    );
}
