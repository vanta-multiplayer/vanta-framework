import { and, eq, gt, isNull } from "drizzle-orm";
import { mintId } from "../crypto/tokens";
import type { Database } from "../db";
import { sessions, type Session } from "../db/schema";

export async function createSession(
  db: Database,
  accountId: string,
  tokenHash: string,
  expiresAt: Date,
): Promise<Session> {
  const [row] = await db
    .insert(sessions)
    .values({
      id: mintId(16),
      accountId,
      tokenHash,
      expiresAt,
    })
    .returning();
  return row!;
}

export async function findValidSessionByTokenHash(
  db: Database,
  tokenHash: string,
): Promise<Session | null> {
  const [row] = await db
    .select()
    .from(sessions)
    .where(
      and(
        eq(sessions.tokenHash, tokenHash),
        isNull(sessions.revokedAt),
        gt(sessions.expiresAt, new Date()),
      ),
    )
    .limit(1);
  return row ?? null;
}
