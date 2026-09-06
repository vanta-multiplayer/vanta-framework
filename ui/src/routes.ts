export const routes = {
    home: "/",
    servers: "/servers",
    settings: "/settings",
    about: "/about",
    connection: "/connection",
    pause: "/pause",
} as const;

export type RoutePath = (typeof routes)[keyof typeof routes];
