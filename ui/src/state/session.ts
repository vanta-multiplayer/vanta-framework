import { createSignal } from "solid-js";
import { emit, inGame, on, type ConnectionProgress, type ServerEntry, type Settings } from "./bridge";
import { asServerEntry } from "../lib/server-entry";
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
const [download, setDownload] = createSignal<ConnectionProgress | null>(null);
const [version, setVersion] = createSignal("0.0.0");
const [branch, setBranch] = createSignal("develop");
const [directOpen, setDirectOpen] = createSignal(false);
const [joinOpen, setJoinOpen] = createSignal(false);
const [joinServer, setJoinServer] = createSignal<ServerEntry | null>(null);
const [inviteOpen, setInviteOpen] = createSignal(false);
const [inviteServer, setInviteServer] = createSignal<ServerEntry | null>(null);
const [exitOpen, setExitOpen] = createSignal(false);
const [restartOpen, setRestartOpen] = createSignal(false);
const [settings, setSettings] = createSignal<Settings | null>(null);
const [errorOpen, setErrorOpen] = createSignal(false);
const [errorTitle, setErrorTitle] = createSignal("Connection failed");
const [errorMessage, setErrorMessage] = createSignal("");
const [voiceDevices, setVoiceDevices] = createSignal<{
    input: {id: string; name: string; isDefault?: boolean}[];
    output: {id: string; name: string; isDefault?: boolean}[];
}>({
    input: [],
    output: [],
});
const [micLevel, setMicLevel] = createSignal(0);
const [micTest, setMicTest] = createSignal(false);

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
    download,
    version,
    branch,
    directOpen,
    joinOpen,
    joinServer,
    inviteOpen,
    inviteServer,
    exitOpen,
    restartOpen,
    settings,
    errorOpen,
    errorTitle,
    errorMessage,
    voiceDevices,
    micLevel,
    micTest,
    setMicTest,
    setNickname,
    setAddress,
    setDirectOpen,
    setJoinOpen,
    setExitOpen,
    setRestartOpen,
    setErrorOpen,
};

export function netgraphEnabled(): boolean {
    return settings()?.netgraph === true;
}

export function setNetgraphEnabled(enabled: boolean): void {
    setSettings((current) => ({...(current ?? {}), netgraph: enabled}));
    emit("settings:change", "netgraph", enabled);
    emit("netgraph:enable", enabled);
}

type BootDisplay = {
    windowWidth: number;
    windowHeight: number;
    borderless: boolean;
    exclusive: boolean;
};

let bootDisplay: BootDisplay | null = null;

function displayFromSettings(data: Settings): BootDisplay {
    return {
        windowWidth: data.windowWidth ?? 0,
        windowHeight: data.windowHeight ?? 0,
        borderless: data.borderless ?? data.fullscreen ?? false,
        exclusive: data.exclusive ?? false,
    };
}

export function notifyDisplaySaved(next: BootDisplay): void {
    if (!bootDisplay) {
        bootDisplay = next;
        return;
    }

    if (
        next.windowWidth !== bootDisplay.windowWidth ||
        next.windowHeight !== bootDisplay.windowHeight ||
        next.borderless !== bootDisplay.borderless ||
        next.exclusive !== bootDisplay.exclusive
    ) {
        setRestartOpen(true);
    }
}

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

function showError(title: string, message: string): void {
    const detail = message.trim();

    if (detail === "") {
        return;
    }

    setErrorTitle(title);
    setErrorMessage(detail);
    setErrorOpen(true);
}

export function handleEscape(): void {
    if (errorOpen()) {
        setErrorOpen(false);
        return;
    }

    if (restartOpen()) {
        setRestartOpen(false);
        return;
    }

    if (exitOpen()) {
        setExitOpen(false);
        return;
    }

    if (inviteOpen()) {
        closeInvite();
        return;
    }

    if (joinOpen()) {
        closeJoin();
        return;
    }

    if (directOpen()) {
        setDirectOpen(false);
        return;
    }

    if (connecting()) {
        emit("connection:abort");
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

export function openJoin(server: ServerEntry): void {
    setJoinServer(server);
    setAddress(server.address);
    setJoinOpen(true);
}

export function closeJoin(): void {
    setJoinOpen(false);
    setJoinServer(null);
}

export function openInvite(server: ServerEntry): void {
    setInviteServer(server);
    setAddress(server.address);
    setInviteOpen(true);
    showMenu(true);
}

export function closeInvite(): void {
    setInviteOpen(false);
    setInviteServer(null);
}

export function connectTo(target: string, password = ""): void {
    const trimmed = target.trim();

    if (trimmed === "") {
        setStatus("Enter an address, for example 127.0.0.1:22005");
        return;
    }

    const colon = trimmed.lastIndexOf(":");
    const host = colon >= 0 ? trimmed.slice(0, colon) : trimmed;
    const port = colon >= 0 ? Number.parseInt(trimmed.slice(colon + 1), 10) || 22005 : 22005;

    setAddress(trimmed);
    setConnecting(true);
    setErrorOpen(false);
    setStatus(`Connecting to ${trimmed}`);
    closeJoin();
    closeInvite();
    setDirectOpen(false);
    emit("connection:connect", host, port, "", "", password, "", nickname().trim() || "Player", false);
}

export function bindSession(): () => void {
    const stops = [
        on("ui:ready", () => setReady(true)),
        on("version:ready", () => setReady(true)),
        on("version:update", (rel: string, nextBranch: string) => {
            setVersion(rel);
            setBranch(nextBranch);
        }),
        on("settings:devices:update", (data: {input?: {id: string; name: string; isDefault?: boolean}[]; output?: {id: string; name: string; isDefault?: boolean}[]}) => {
            setVoiceDevices({
                input: Array.isArray(data?.input) ? data.input : [],
                output: Array.isArray(data?.output) ? data.output : [],
            });
        }),
        on("settings:currentVolume:update", (value: number) => {
            setMicLevel(typeof value === "number" ? value : 0);
        }),
        on("settings:currentVolume:running", (running: boolean) => {
            setMicTest(!!running);
            if (!running) {
                setMicLevel(0);
            }
        }),
        on("settings:update", (data: Settings) => {
            setSettings((current) => ({...(current ?? {}), ...data}));
            if (!bootDisplay) {
                bootDisplay = displayFromSettings(data);
            }
            setNickname(data.nickname ?? data.name ?? "Player");

            if (data.lastServer) {
                setAddress(data.lastServer);
            }
        }),
        on("servers:update", (list: ServerEntry[] | null) => setServers(list ?? [])),
        on("servers:recent:update", (list: ServerEntry[] | null) => setRecent(list ?? [])),
        on("servers:favorite:update", (list: ServerEntry[] | null) => setFavorites(list ?? [])),
        on("connection:invite", (payload: unknown) => {
            const server = asServerEntry(payload);

            if (server) {
                openInvite(server);
            }
        }),
        on("connection:connected", () => {
            setConnecting(false);
            setConnected(true);
            setDownload(null);
            setStatus("Connected");
            showMenu(false);
        }),
        on("connection:failed", (message: string) => {
            setConnecting(false);
            setConnected(false);
            setDownload(null);
            setStatus(message);
            showMenu(true);
            showError("Could not connect", message);
        }),
        on("connection:connecting", (serverName?: string) => {
            setConnecting(true);
            setConnected(false);
            setDownload(null);
            setStatus(serverName ? `Connecting to ${serverName}` : "Connecting");
            showMenu(true);
        }),
        on("connection:progress", (payload: ConnectionProgress) => {
            setConnecting(true);
            setDownload(payload);

            if (payload.phase === "authenticating") {
                setStatus("Authenticating");
                return;
            }

            if (payload.phase === "downloading") {
                const file = payload.currentFile ? ` — ${payload.currentFile}` : "";
                const count =
                    payload.filesTotal > 0 ? ` (${payload.filesDownloaded}/${payload.filesTotal})` : "";
                setStatus(`Downloading resources${count}${file}`);
                return;
            }

            if (payload.phase === "starting") {
                setStatus("Starting");
            }
        }),
        on("connection:disconnected", (message: string) => {
            setConnecting(false);
            setConnected(false);
            setDownload(null);
            setStatus(message);
            goTo(routes.home);
            showMenu(true);

            const detail = (message ?? "").trim();

            if (detail !== "" && detail !== "Disconnected" && detail !== "Cancelled") {
                showError("Disconnected", detail);
            }
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
            {
                name: "Local",
                address: "127.0.0.1:22005",
                players: 0,
                maxPlayers: 32,
                ping: 12,
                language: "English",
                gamemode: "Freeroam",
                tags: ["Freeroam", "Dev"],
                description: "Your machine. Resources load from this host.",
            },
            {
                name: "Night city",
                address: "192.168.1.10:22005",
                players: 14,
                maxPlayers: 48,
                ping: 38,
                language: "English",
                gamemode: "Roleplay",
                website: "night.city",
                tags: ["Roleplay", "Voice", "Vehicles"],
                description: "A city that does not sleep.",
            },
        ]);
        setRecent([
            {
                name: "Local",
                address: "127.0.0.1:22005",
                players: 0,
                maxPlayers: 32,
                ping: 12,
                language: "English",
                gamemode: "Freeroam",
                tags: ["Freeroam", "Dev"],
                description: "Your machine. Resources load from this host.",
            },
        ]);
        setFavorites([
            {
                name: "Night city",
                address: "192.168.1.10:22005",
                players: 14,
                maxPlayers: 48,
                ping: 38,
                language: "English",
                gamemode: "Roleplay",
                website: "night.city",
                tags: ["Roleplay", "Voice", "Vehicles"],
                description: "A city that does not sleep.",
            },
        ]);

        if (window.location.hash.includes("invite")) {
            openInvite({
                name: "Night city",
                address: "192.168.1.10:22005",
                players: 14,
                maxPlayers: 48,
                ping: 38,
                language: "English",
                website: "night.city",
                tags: ["Roleplay", "Voice"],
                description: "A city that does not sleep.",
            });
        }
        setSettings({
            nickname: "Player",
            lastServer: "127.0.0.1:22005",
            windowWidth: 1280,
            windowHeight: 720,
            borderless: false,
            exclusive: false,
            fullscreen: false,
            volumeMaster: 1,
            volumeSfx: 1,
            volumeRadio: 1,
            voiceInputDevice: "default",
            voiceOutputDevice: "default",
            voiceVolume: 1,
            voicePttKey: 0x56,
            netgraph: false,
            display: 0,
            modes: [
                {width: 1280, height: 720, label: "1280×720"},
                {width: 1920, height: 1080, label: "1920×1080"},
            ],
            displays: [{index: 0, width: 1920, height: 1080, primary: true, name: "Display 1"}],
        });
        setVoiceDevices({
            input: [
                {id: "{0.0.1.00000000}.{11111111-1111-1111-1111-111111111111}", name: "Microphone (Realtek)", isDefault: true},
                {id: "{0.0.1.00000000}.{22222222-2222-2222-2222-222222222222}", name: "Headset Mic"},
            ],
            output: [
                {id: "{0.0.0.00000000}.{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}", name: "Speakers (Realtek)", isDefault: true},
                {id: "{0.0.0.00000000}.{bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb}", name: "Headphones"},
            ],
        });
    }

    emit("loaded");

    return () => {
        for (const stop of stops) {
            stop();
        }
    };
}
