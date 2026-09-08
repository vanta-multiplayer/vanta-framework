import { getRedis } from "./redis";

export type CachedUserInfo = {
  sub: string;
  name: string;
};

function userInfoKey(tokenHash: string): string {
  return `vanta:auth:userinfo:${tokenHash}`;
}

/** Cache Epic UserInfo lookups (hot path). Postgres remains source of truth. */
export async function cacheUserInfo(
  tokenHash: string,
  info: CachedUserInfo,
  ttlSeconds: number,
): Promise<void> {
  const redis = getRedis();
  if (!redis || ttlSeconds <= 0) return;
  try {
    await redis.set(userInfoKey(tokenHash), JSON.stringify(info), "EX", ttlSeconds);
  } catch (err) {
    console.warn("[auth] redis set userinfo failed", err instanceof Error ? err.message : err);
  }
}

export async function getCachedUserInfo(tokenHash: string): Promise<CachedUserInfo | null> {
  const redis = getRedis();
  if (!redis) return null;
  try {
    const raw = await redis.get(userInfoKey(tokenHash));
    if (!raw) return null;
    const parsed = JSON.parse(raw) as CachedUserInfo;
    if (!parsed?.sub || !parsed?.name) return null;
    return parsed;
  } catch (err) {
    console.warn("[auth] redis get userinfo failed", err instanceof Error ? err.message : err);
    return null;
  }
}

export async function invalidateUserInfo(tokenHash: string): Promise<void> {
  const redis = getRedis();
  if (!redis) return;
  try {
    await redis.del(userInfoKey(tokenHash));
  } catch {
    // ignore
  }
}
