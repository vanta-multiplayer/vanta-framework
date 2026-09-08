import { Elysia } from "elysia";
import { z } from "zod";
import type { AuthConfig } from "../config";
import type { Database } from "../db";
import {
  issueAccessToken,
  registerInstallation,
  validateInstallationId,
  validateSecret,
} from "../services/identity";
import { clientIp, rateLimitAllow } from "../services/rate_limit";
import { AppError, ErrorCode } from "../utils/errors";
import { fail, failCode, handleRouteError, ok } from "../utils/http";

const bodySchema = z.object({
  installationId: z.string().min(16).max(128),
  installationSecret: z.string().min(32).max(256),
  displayName: z.string().min(1).max(64).optional(),
});

function parseBody(raw: unknown) {
  const obj = (raw ?? {}) as Record<string, unknown>;
  return bodySchema.safeParse({
    installationId: obj.installationId ?? obj.installation_id ?? obj.install_id,
    installationSecret: obj.installationSecret ?? obj.installation_secret ?? obj.install_secret,
    displayName: obj.displayName ?? obj.display_name,
  });
}

export function identityRoutes(db: Database, config: AuthConfig) {
  return new Elysia({ prefix: "/identity" })
    .post("/register", async ({ request, body, set }) => {
      const ip = clientIp(request);
      const limited = rateLimitAllow(
        `register:${ip}`,
        config.rateLimitMaxRegister,
        config.rateLimitWindowMs,
      );
      if (!limited.ok) {
        return failCode(set, ErrorCode.RATE_LIMITED, 429);
      }

      const parsed = parseBody(body);
      if (!parsed.success) {
        return failCode(set, ErrorCode.INVALID_BODY, 400, parsed.error.flatten());
      }

      const { installationId, installationSecret, displayName } = parsed.data;
      if (!validateInstallationId(installationId) || !validateSecret(installationSecret)) {
        return failCode(set, ErrorCode.INVALID_CREDENTIALS, 400);
      }

      try {
        const result = await registerInstallation(db, installationId, installationSecret, displayName);
        return ok(set, result, result.created ? 201 : 200);
      } catch (err) {
        return handleRouteError(set, err);
      }
    })
    .post("/token", async ({ request, body, set }) => {
      const ip = clientIp(request);
      const limited = rateLimitAllow(`token:${ip}`, config.rateLimitMaxToken, config.rateLimitWindowMs);
      if (!limited.ok) {
        return failCode(set, ErrorCode.RATE_LIMITED, 429);
      }

      const parsed = parseBody(body);
      if (!parsed.success) {
        return failCode(set, ErrorCode.INVALID_BODY, 400, parsed.error.flatten());
      }

      const { installationId, installationSecret, displayName } = parsed.data;
      if (!validateInstallationId(installationId) || !validateSecret(installationSecret)) {
        return failCode(set, ErrorCode.INVALID_CREDENTIALS, 400);
      }

      try {
        const result = await issueAccessToken(
          db,
          config,
          installationId,
          installationSecret,
          displayName,
        );
        return ok(set, {
          accessToken: result.accessToken,
          tokenType: "Bearer" as const,
          expiresIn: result.expiresIn,
          accountId: result.accountId,
        });
      } catch (err) {
        // Don't distinguish missing vs bad secret to callers.
        if (err instanceof AppError && err.code === ErrorCode.INSTALLATION_AUTH_FAILED) {
          return failCode(set, ErrorCode.UNAUTHORIZED, 401);
        }
        return handleRouteError(set, err);
      }
    });
}
