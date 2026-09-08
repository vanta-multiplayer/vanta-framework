import { and, eq, isNull } from "drizzle-orm";
import type { Database } from "../db";
import { installations, type Installation } from "../db/schema";

export async function insertInstallation(
  db: Database,
  installationId: string,
  accountId: string,
  credentialHash: string,
): Promise<Installation> {
  const [row] = await db
    .insert(installations)
    .values({
      id: installationId,
      accountId,
      credentialHash,
    })
    .returning();
  return row!;
}

export async function findActiveInstallation(
  db: Database,
  installationId: string,
): Promise<Installation | null> {
  const [row] = await db
    .select()
    .from(installations)
    .where(and(eq(installations.id, installationId), isNull(installations.revokedAt)))
    .limit(1);
  return row ?? null;
}

export async function touchInstallation(db: Database, installationId: string): Promise<void> {
  await db
    .update(installations)
    .set({ lastUsedAt: new Date() })
    .where(eq(installations.id, installationId));
}
