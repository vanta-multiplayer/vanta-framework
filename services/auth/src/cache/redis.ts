import Redis from "ioredis";

let client: Redis | null = null;
let enabled = false;

export type CacheStatus = "disabled" | "ready" | "error";

let lastError: string | null = null;

export async function initRedis(redisUrl: string | null): Promise<void> {
  if (!redisUrl) {
    enabled = false;
    client = null;
    console.info("[auth] Redis cache disabled (REDIS_URL not set)");
    return;
  }

  const redis = new Redis(redisUrl, {
    maxRetriesPerRequest: 2,
    enableReadyCheck: true,
    lazyConnect: true,
  });

  redis.on("error", (err) => {
    lastError = err instanceof Error ? err.message : String(err);
  });

  try {
    await redis.connect();
    await redis.ping();
    client = redis;
    enabled = true;
    lastError = null;
    console.info("[auth] Redis cache ready");
  } catch (err) {
    lastError = err instanceof Error ? err.message : String(err);
    enabled = false;
    client = null;
    try {
      redis.disconnect();
    } catch {
      // ignore
    }
    console.warn(`[auth] Redis unavailable — continuing without cache (${lastError})`);
  }
}

export function getRedis(): Redis | null {
  return enabled ? client : null;
}

export function redisStatus(): { status: CacheStatus; error: string | null } {
  if (!enabled || !client) {
    return { status: lastError ? "error" : "disabled", error: lastError };
  }
  return { status: "ready", error: null };
}

export async function closeRedis(): Promise<void> {
  if (client) {
    await client.quit().catch(() => client?.disconnect());
    client = null;
  }
  enabled = false;
}
