import { eq } from "drizzle-orm";
import { mintId } from "../crypto/tokens";
import type { Database } from "../db";
import { accounts, type Account } from "../db/schema";

export async function createAccount(db: Database, displayName: string): Promise<Account> {
  const id = mintId(16);
  const name = displayName.trim() || `Player-${id.slice(0, 8)}`;
  const [row] = await db
    .insert(accounts)
    .values({ id, displayName: name, status: "active" })
    .returning();
  return row!;
}

export async function getActiveAccount(db: Database, accountId: string): Promise<Account | null> {
  const [row] = await db
    .select()
    .from(accounts)
    .where(eq(accounts.id, accountId))
    .limit(1);
  if (!row || row.status !== "active") {
    return null;
  }
  return row;
}

export async function updateDisplayName(
  db: Database,
  accountId: string,
  displayName: string,
): Promise<void> {
  const name = displayName.trim();
  if (!name) return;
  await db.update(accounts).set({ displayName: name }).where(eq(accounts.id, accountId));
}
