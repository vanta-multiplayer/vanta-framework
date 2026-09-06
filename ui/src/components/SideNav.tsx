import type {JSX} from "solid-js";
import {Show} from "solid-js";
import {A, useLocation} from "@solidjs/router";
import House from "lucide-solid/icons/house";
import Server from "lucide-solid/icons/server";
import Unplug from "lucide-solid/icons/unplug";
import Settings from "lucide-solid/icons/settings";
import Info from "lucide-solid/icons/info";
import LogOut from "lucide-solid/icons/log-out";
import {IconMark} from "./icons";
import {routes} from "../routes";
import {branch, connected, connecting, disconnect, setDirectOpen, setExitOpen} from "../state/session";

function NavLink(props: {
    href: string;
    label: string;
    disabled?: boolean;
    end?: boolean;
    children: JSX.Element;
}): JSX.Element {
    return (
        <A
            href={props.href}
            title={props.label}
            end={props.end}
            class="nav-item"
            activeClass="nav-item--active"
            classList={{"nav-item--disabled": props.disabled}}
            onClick={(event) => {
                if (props.disabled) {
                    event.preventDefault();
                }
            }}
        >
            {props.children}
        </A>
    );
}

export function SideNav(): JSX.Element {
    const location = useLocation();
    const busy = () => connecting();

    return (
        <nav class="nav">
            <div class="nav__group">
                <Show when={branch() !== "release"}>
                    <div class="nav__branch">{branch()}</div>
                </Show>
                <div class="nav__logo">
                    <IconMark />
                </div>
                <Show when={!connected()}>
                    <NavLink href={routes.home} label="Home" end disabled={busy()}>
                        <House />
                    </NavLink>
                    <NavLink href={routes.servers} label="Servers" disabled={busy()}>
                        <Server />
                    </NavLink>
                    <button
                        type="button"
                        title="Direct connect"
                        disabled={busy()}
                        class="nav-item"
                        classList={{"nav-item--disabled": busy()}}
                        onClick={() => setDirectOpen(true)}
                    >
                        <Unplug />
                    </button>
                </Show>
            </div>
            <div class="nav__group">
                <Show when={connected()}>
                    <NavLink href={routes.pause} label="Paused">
                        <House />
                    </NavLink>
                    <button type="button" title="Disconnect" class="nav-item" onClick={disconnect}>
                        <Unplug />
                    </button>
                </Show>
                <Show when={busy() || location.pathname === routes.connection}>
                    <NavLink href={routes.connection} label="Connection">
                        <Unplug />
                    </NavLink>
                </Show>
                <NavLink href={routes.settings} label="Settings">
                    <Settings />
                </NavLink>
                <NavLink href={routes.about} label="About">
                    <Info />
                </NavLink>
                <button type="button" title="Exit" class="nav-item" onClick={() => setExitOpen(true)}>
                    <LogOut />
                </button>
            </div>
        </nav>
    );
}
