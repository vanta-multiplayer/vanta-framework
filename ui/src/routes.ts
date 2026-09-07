export const routes = {
    home: "/",
    servers: "/servers",
    settings: "/settings",
    about: "/about",
    pause: "/pause",
} as const;

export type RoutePath = (typeof routes)[keyof typeof routes];
