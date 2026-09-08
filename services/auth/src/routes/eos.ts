import { Elysia } from "elysia";
import type { Database } from "../db";
import { resolveUserInfo } from "../services/identity";
import { extractBearer } from "../utils/bearer";
import { ErrorCode } from "../utils/errors";
import { eosUnauthorized, eosUserInfo } from "../utils/http";

export function eosRoutes(db: Database) {
  return new Elysia({ prefix: "/eos" }).get("/userinfo", async ({ request, set }) => {
    const token = extractBearer(request.headers.get("authorization"));
    if (!token) {
      return eosUnauthorized(set, ErrorCode.MISSING_BEARER);
    }

    const info = await resolveUserInfo(db, token);
    if (!info) {
      return eosUnauthorized(set, ErrorCode.INVALID_TOKEN);
    }

    console.info(`[auth] UserInfo OK (sub length=${info.sub.length})`);
    return eosUserInfo(set, info);
  });
}
