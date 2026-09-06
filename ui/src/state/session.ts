import { createSignal } from "solid-js";
import { emit, inGame, on, type ServerEntry, type Settings } from "./bridge";
import { claimFocus } from "./focus";
import { routes, type RoutePath } from "../routes";

const [ready, setReady] = createSignal(false);
const [menuOpen, setMenuOpen] = createSignal(true);
const [connected, setConnected] = createSignal(false);
const [nickname, setNickname] = createSignal("Player");
const [address, setAddress] = createSignal("127.0.0.1:22005");
const [servers, setServers] = createSignal<ServerEntry[]>([]);
const [recent, setRecent] = createSignal<ServerEntry[]>([]);
const [favorites, setFavorites] = createSignal<ServerEntry[]>([]);
const [status, setStatus] = createSignal("");
const [connecting, setConnecting] = createSignal(false);
const [version, setVersion] = createSignal("0.0.0");
const [branch, setBranch] = createSignal("develop");
const [directOpen, setDirectOpen] = createSignal(false);
const [exitOpen, setExitOpen] = createSignal(false);
const [settings, setSettings] = createSignal<Settings | null>(null);

export {
    ready,
    menuOpen,
    connected,
    nickname,
    address,
    servers,
    recent,
    favorites,
    status,
    connecting,
    version,
    branch,
    directOpen,
    exitOpen,
    settings,
    setNickname,
    setAddress,
    setDirectOpen,
    setExitOpen,
};

let navigateTo: (path: RoutePath) => void = () => {};

export function bindNavigate(navigate: (path: string) => void): void {
    navigateTo = (path) => navigate(path);
}

export function goTo(path: RoutePath): void {
    navigateTo(path);
}

export function persistNickname(value: string): void {
    setNickname(value);
    emit("settings:change", "nickname", value);
}

export function showMenu(visible: boolean): void {
    // Offline the menu is the product. Hiding it leaves a black screen and no
    // way back, because CEF then stops receiving keys.
    if (!visible && !connected() && !connecting()) {
        return;
    }

    const opening = visible && !menuOpen();

    if (opening && connected()) {
        goTo(routes.pause);
    }

    if (menuOpen() === visible) {
        return;
    }

    setMenuOpen(visible);
    claimFocus("menu", visible, visible);
    emit("ui:toggle", visible);
}

export function disconnect(): void {
    emit("connection:disconnect");
}

export function handleEscape(): void {
    if (exitOpen()) {
        setExitOpen(false);
        return;
    }

    if (directOpen()) {
        setDirectOpen(false);
        return;
    }

    if (!connected()) {
        return;
    }

    const path = window.location.hash.replace(/^#/, "") || "/";

    if (path === routes.settings || path === routes.about) {
        goTo(routes.pause);
        return;
    }

    showMenu(false);
}

export function connectTo(target: string): void {
    const trimmed = target.trim();

    if (trimmed === "") {
        setStatus("Enter an address, for example 127.0.0.1:22005");
        return;
    }

    const colon = trimmed.lastIndexOf(":");
    const host = colon >= 0 ? trimmed.slice(0, colon) : trimmed;
    const port = colon >= 0 ? Number.parseInt(trimmed.slice(colon + 1), 10) || 22005 : 22005;

    setConnecting(true);
    setStatus(`Connecting to ${trimmed}`);
    goTo(routes.connection);
    setDirectOpen(false);
    emit("connection:connect", host, port, "", "", "", "", nickname().trim() || "Player", false);
}

export function bindSession(): () => void {
    const stops = [
        on("ui:ready", () => setReady(true)),
        on("version:ready", () => setReady(true)),
        on("version:update", (rel: string, nextBranch: string) => {
            setVersion(rel);
            setBranch(nextBranch);
        }),
        on("settings:update", (data: Settings) => {
            setSettings(data);
            setNickname(data.nickname ?? data.name ?? "Player");

            if (data.lastServer) {
                setAddress(data.lastServer);
            }
        }),
        on("servers:update", (list: ServerEntry[] | null) => setServers(list ?? [])),
        on("servers:recent:update", (list: ServerEntry[] | null) => setRecent(list ?? [])),
        on("servers:favorite:update", (list: ServerEntry[] | null) => setFavorites(list ?? [])),
        on("connection:connected", () => {
            setConnecting(false);
            setConnected(true);
            setStatus("Connected");
            showMenu(false);
        }),
        on("connection:failed", (message: string) => {
            setConnecting(false);
            setConnected(false);
            setStatus(message);
            goTo(routes.connection);
            showMenu(true);
        }),
        on("connection:connecting", (serverName?: string) => {
            setConnecting(true);
            setConnected(false);
            setStatus(serverName ? `Connecting to ${serverName}` : "Connecting");
            goTo(routes.connection);
            showMenu(true);
        }),
        on("connection:disconnected", (message: string) => {
            setConnecting(false);
            setConnected(false);
            setStatus(message);
            goTo(routes.home);
            showMenu(true);
        }),
        on("ui:toggle", (state?: boolean) => {
            if (state !== undefined) {
                showMenu(state);
            }
        }),
        on("hotkey:escape", () => handleEscape()),
    ];

    claimFocus("menu", true, true);

    if (!inGame()) {
        setReady(true);
        setVersion("0.1.0");
        setBranch("develop");
        setServers([
            {name: "Local", address: "127.0.0.1:22005", players: 0, maxPlayers: 32, ping: 12},
            {name: "Night city", address: "192.168.1.10:22005", players: 14, maxPlayers: 48, ping: 38},
        ]);
        setRecent([{name: "Local", address: "127.0.0.1:22005", players: 0, maxPlayers: 32, ping: 12}]);
        setSettings({
            nickname: "Player",
            lastServer: "127.0.0.1:22005",
            windowWidth: 1280,
            windowHeight: 720,
            fullscreen: false,
            display: 0,
            modes: [
                {width: 1280, height: 720, label: "1280×720"},
                {width: 1920, height: 1080, label: "1920×1080"},
            ],
            displays: [{index: 0, width: 1920, height: 1080, primary: true, name: "Display 1"}],
        });
    }

    emit("loaded");

    return () => {
        for (const stop of stops) {
            stop();
        }
    };
}
