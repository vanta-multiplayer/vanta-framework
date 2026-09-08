import { migrate } from "drizzle-orm/node-postgres/migrator";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { loadConfig } from "../config";
import { closeDb, createDb } from "./index";

const config = loadConfig();
const db = createDb(config.databaseUrl);
const migrationsFolder = path.join(path.dirname(fileURLToPath(import.meta.url)), "migrations");

await migrate(db, { migrationsFolder });
console.info("[auth] drizzle migrations complete");
await closeDb();
process.exit(0);
