import { migrate } from "drizzle-orm/node-postgres/migrator";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { Elysia } from "elysia";
import { closeRedis, initRedis } from "./cache/redis";
import { loadConfig } from "./config";
import { closeDb, createDb } from "./db";
import { eosRoutes } from "./routes/eos";
import { healthRoutes } from "./routes/health";
import { identityRoutes } from "./routes/identity";
import { ErrorCode } from "./utils/errors";
import { failCode, fromUnknown } from "./utils/http";

const config = loadConfig();
const db = createDb(config.databaseUrl);

const migrationsFolder = path.join(path.dirname(fileURLToPath(import.meta.url)), "db", "migrations");
await migrate(db, { migrationsFolder });
await initRedis(config.redisUrl);

const app = new Elysia()
  .onError(({ code, error, set }) => {
    if (code === "NOT_FOUND") {
      return failCode(set, ErrorCode.NOT_FOUND, 404);
    }
    return fromUnknown(set, error);
  })
  .use(healthRoutes(config))
  .use(identityRoutes(db, config))
  .use(eosRoutes(db));

app.listen({
  hostname: config.host,
  port: config.port,
});

console.info(
  `[auth] Vanta Auth Backend listening on http://${config.host}:${config.port} (publicUrl=${config.publicUrl})`,
);
console.info(`[auth] endpoints: GET /health , POST /identity/register , POST /identity/token , GET /eos/userinfo`);
console.info(
  `[auth] Epic OpenID UserInfo must be HTTPS public URL → ${config.publicUrl}/eos/userinfo (not localhost)`,
);

async function shutdown() {
  console.info("[auth] shutting down");
  await closeRedis();
  await closeDb();
  process.exit(0);
}

process.on("SIGINT", () => void shutdown());
process.on("SIGTERM", () => void shutdown());
