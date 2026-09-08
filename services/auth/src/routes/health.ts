import { Elysia } from "elysia";
import type { AuthConfig } from "../config";
import { redisStatus } from "../cache/redis";
import { ok } from "../utils/http";

export function healthRoutes(config: AuthConfig) {
  return new Elysia().get("/health", ({ set }) => {
    const cache = redisStatus();
    return ok(set, {
      service: "vanta-auth",
      publicUrl: config.publicUrl,
      cache: {
        status: cache.status,
        ...(cache.error ? { error: cache.error } : {}),
      },
    });
  });
}
