import { randomBytes } from "node:crypto";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";

const root = path.join(import.meta.dir, "..");
const envPath = path.join(root, ".env");
const examplePath = path.join(root, ".env.example");
const PUBLIC_URL = "https://id.vantamp.xyz";
const rotate = process.argv.includes("--rotate");

function secret(bytes = 24): string {
  return randomBytes(bytes).toString("base64url");
}

function parseEnv(text: string): Map<string, string> {
  const map = new Map<string, string>();
  for (const line of text.split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith("#")) continue;
    const eq = trimmed.indexOf("=");
    if (eq <= 0) continue;
    map.set(trimmed.slice(0, eq), trimmed.slice(eq + 1));
  }
  return map;
}

function renderEnv(map: Map<string, string>, order: string[]): string {
  const lines: string[] = [
    "# Generated/updated by: bun run secrets:gen",
    "# Do not commit this file.",
    "# Postgres only reads POSTGRES_PASSWORD on first volume init.",
    "# After rotating DB secrets: docker compose down && docker volume rm <project>_vanta_auth_pg",
    "",
  ];
  const written = new Set<string>();
  for (const key of order) {
    if (!map.has(key)) continue;
    lines.push(`${key}=${map.get(key)}`);
    written.add(key);
  }
  for (const [key, value] of map) {
    if (written.has(key)) continue;
    lines.push(`${key}=${value}`);
  }
  lines.push("");
  return lines.join("\n");
}

const order = [
  "VANTA_AUTH_PUBLIC_URL",
  "VANTA_AUTH_HOST",
  "VANTA_AUTH_PORT",
  "POSTGRES_USER",
  "POSTGRES_PASSWORD",
  "POSTGRES_DB",
  "DATABASE_URL",
  "REDIS_PASSWORD",
  "REDIS_URL",
  "ACCESS_TOKEN_TTL_SECONDS",
  "RATE_LIMIT_WINDOW_MS",
  "RATE_LIMIT_MAX_REGISTER",
  "RATE_LIMIT_MAX_TOKEN",
  "CERTBOT_EMAIL",
  "CERTBOT_DOMAIN",
];

const baseText = existsSync(envPath)
  ? readFileSync(envPath, "utf8")
  : existsSync(examplePath)
    ? readFileSync(examplePath, "utf8")
    : "";

const map = parseEnv(baseText);

const postgresUser = map.get("POSTGRES_USER") || "vanta";
const postgresDb = map.get("POSTGRES_DB") || "vanta_auth";

const existingPg = map.get("POSTGRES_PASSWORD")?.trim();
const existingRedis = map.get("REDIS_PASSWORD")?.trim();
const postgresPassword =
  !rotate && existingPg ? existingPg : secret(32);
const redisPassword = !rotate && existingRedis ? existingRedis : secret(32);

const existingPublic = map.get("VANTA_AUTH_PUBLIC_URL") || "";
const publicUrl =
  !existingPublic ||
  existingPublic.includes("127.0.0.1") ||
  existingPublic.includes("localhost") ||
  existingPublic.includes("CHANGE_ME")
    ? PUBLIC_URL
    : existingPublic;

map.set("VANTA_AUTH_PUBLIC_URL", publicUrl);
map.set("VANTA_AUTH_HOST", "0.0.0.0");
map.set("VANTA_AUTH_PORT", "28080");
map.set("POSTGRES_USER", postgresUser);
map.set("POSTGRES_PASSWORD", postgresPassword);
map.set("POSTGRES_DB", postgresDb);
// Host-oriented URLs for local tooling; compose overrides for the auth container.
map.set(
  "DATABASE_URL",
  `postgres://${postgresUser}:${encodeURIComponent(postgresPassword)}@127.0.0.1:5432/${postgresDb}`,
);
map.set("REDIS_PASSWORD", redisPassword);
map.set("REDIS_URL", `redis://:${encodeURIComponent(redisPassword)}@127.0.0.1:6379/0`);
map.set("ACCESS_TOKEN_TTL_SECONDS", map.get("ACCESS_TOKEN_TTL_SECONDS") || "600");
map.set("RATE_LIMIT_WINDOW_MS", map.get("RATE_LIMIT_WINDOW_MS") || "60000");
map.set("RATE_LIMIT_MAX_REGISTER", map.get("RATE_LIMIT_MAX_REGISTER") || "20");
map.set("RATE_LIMIT_MAX_TOKEN", map.get("RATE_LIMIT_MAX_TOKEN") || "60");
map.set("CERTBOT_EMAIL", map.get("CERTBOT_EMAIL") || "admin@vantamp.xyz");
map.set("CERTBOT_DOMAIN", map.get("CERTBOT_DOMAIN") || "id.vantamp.xyz");
map.delete("NGINX_HTTP_PORT");

writeFileSync(envPath, renderEnv(map, order), "utf8");

console.info(`[auth] wrote ${envPath}`);
if (rotate || !existingPg || !existingRedis) {
  console.info("[auth] POSTGRES_PASSWORD / REDIS_PASSWORD updated");
} else {
  console.info("[auth] kept existing POSTGRES_PASSWORD / REDIS_PASSWORD (use --rotate to replace)");
}
console.info("[auth] VANTA_AUTH_PUBLIC_URL ->", publicUrl);
if (rotate) {
  console.info("[auth] Postgres ignores password changes on existing volumes. Wipe DB data:");
  console.info("[auth]   docker compose down");
  console.info("[auth]   docker volume ls | grep vanta_auth_pg");
  console.info("[auth]   docker volume rm <that_volume_name>");
  console.info("[auth]   docker compose up -d");
} else {
  console.info("[auth] next: bun run certs:dev && docker compose up -d --build");
}
