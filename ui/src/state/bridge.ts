export { emit, on, inGame } from "../lib/vanta";
export { default as vanta } from "../lib/vanta";

export interface ServerEntry {
    readonly id?: string;
    readonly name: string;
    readonly address: string;
    readonly players: number;
    readonly maxPlayers: number;
    readonly ping: number;
    readonly language?: string;
    readonly gamemode?: string;
    readonly website?: string;
    readonly description?: string;
    readonly tags?: readonly string[];
    readonly banner?: string;
    readonly avatar?: string;
}

export interface ConnectionProgress {
    readonly phase: string;
    readonly downloading: boolean;
    readonly progress: number;
    readonly filesDownloaded: number;
    readonly filesTotal: number;
    readonly bytesDownloaded: number;
    readonly bytesTotal: number;
    readonly currentFile: string;
}

export interface NetgraphSample {
    readonly fps: number;
    readonly rtt: number;
    readonly tx: number;
    readonly rx: number;
    readonly connected: boolean;
}

export type ConsoleLevel = "debug" | "info" | "warn" | "error" | "fatal";

export interface ConsoleLine {
    readonly level: ConsoleLevel;
    readonly category: string;
    readonly message: string;
    readonly timestamp: string;
}

export interface Settings {
    readonly nickname?: string;
    readonly lastServer?: string;
    readonly name?: string;
    readonly windowWidth?: number;
    readonly windowHeight?: number;
    readonly borderless?: boolean;
    readonly exclusive?: boolean;
    readonly fullscreen?: boolean;
    readonly volumeMaster?: number;
    readonly volumeSfx?: number;
    readonly volumeRadio?: number;
    readonly voiceInputDevice?: string;
    readonly voiceOutputDevice?: string;
    readonly voiceVolume?: number;
    readonly voicePttKey?: number;
    readonly netgraph?: boolean;
    readonly display?: number;
    readonly modes?: readonly { width: number; height: number; label: string }[];
    readonly displays?: readonly {
        index: number;
        width: number;
        height: number;
        primary: boolean;
        name: string;
    }[];
}
