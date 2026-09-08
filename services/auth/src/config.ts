function required(name: string): string {
  const value = process.env[name]?.trim();
  if (!value) {
    throw new Error(`Missing required env var: ${name}`);
  }
  return value;
}

function intEnv(name: string, fallback: number): number {
  const raw = process.env[name]?.trim();
  if (!raw) return fallback;
  const n = Number.parseInt(raw, 10);
  if (!Number.isFinite(n) || n <= 0) {
    throw new Error(`Invalid integer env var ${name}: ${raw}`);
  }
  return n;
}

export type AuthConfig = {
  publicUrl: string;
  host: string;
  port: number;
  databaseUrl: string;
  redisUrl: string | null;
  accessTokenTtlSeconds: number;
  rateLimitWindowMs: number;
  rateLimitMaxRegister: number;
  rateLimitMaxToken: number;
};

export function loadConfig(): AuthConfig {
  const publicUrl = (process.env.VANTA_AUTH_PUBLIC_URL ?? "http://127.0.0.1:28080").replace(/\/$/, "");
  return {
    publicUrl,
    host: process.env.VANTA_AUTH_HOST?.trim() || "127.0.0.1",
    port: intEnv("VANTA_AUTH_PORT", 28080),
    databaseUrl: required("DATABASE_URL"),
    redisUrl: process.env.REDIS_URL?.trim() || null,
    accessTokenTtlSeconds: intEnv("ACCESS_TOKEN_TTL_SECONDS", 600),
    rateLimitWindowMs: intEnv("RATE_LIMIT_WINDOW_MS", 60_000),
    rateLimitMaxRegister: intEnv("RATE_LIMIT_MAX_REGISTER", 20),
    rateLimitMaxToken: intEnv("RATE_LIMIT_MAX_TOKEN", 60),
  };
}
