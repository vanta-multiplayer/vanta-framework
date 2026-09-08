import { drizzle } from "drizzle-orm/node-postgres";
import { Pool } from "pg";
import * as schema from "./schema";

export type Database = ReturnType<typeof createDb>;

let pool: Pool | null = null;
let db: Database | null = null;

export function createDb(databaseUrl: string): Database {
  if (!pool) {
    pool = new Pool({ connectionString: databaseUrl });
  }
  if (!db) {
    db = drizzle(pool, { schema });
  }
  return db;
}

export function getDb(): Database {
  if (!db) {
    throw new Error("Database not initialized — call createDb() first");
  }
  return db;
}

export async function closeDb(): Promise<void> {
  if (pool) {
    await pool.end();
    pool = null;
    db = null;
  }
}
