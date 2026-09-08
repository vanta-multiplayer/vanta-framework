import { AppError, ErrorCode, isAppError, type ErrorCode as Code } from "./errors";

type SetStatus = { status?: number | string };

export type ErrorBody = {
  ok: false;
  error: {
    code: Code;
    message: string;
    details?: unknown;
  };
};

export type OkBody<T> = {
  ok: true;
  data: T;
};

/** Standard success envelope for Vanta Auth APIs (not Epic UserInfo). */
export function ok<T>(set: SetStatus, data: T, status = 200): OkBody<T> {
  set.status = status;
  return { ok: true, data };
}

/** Standard error envelope. */
export function fail(set: SetStatus, error: AppError): ErrorBody {
  set.status = error.status;
  return {
    ok: false,
    error: {
      code: error.code,
      message: error.message,
      ...(error.details !== undefined ? { details: error.details } : {}),
    },
  };
}

export function failCode(set: SetStatus, code: Code, status: number, details?: unknown): ErrorBody {
  return fail(set, new AppError(code, status, details));
}

/**
 * Epic OpenID UserInfo must return a flat `{ sub, name }` body on success.
 * Do not wrap this in the Vanta envelope.
 */
export function eosUserInfo(
  set: SetStatus,
  payload: { sub: string; name: string },
): { sub: string; name: string } {
  set.status = 200;
  return payload;
}

/** Epic treats any non-200 UserInfo as token validation failure; keep body minimal. */
export function eosUnauthorized(set: SetStatus, code: Code = ErrorCode.INVALID_TOKEN): ErrorBody {
  return failCode(set, code, 401);
}

export function fromUnknown(set: SetStatus, err: unknown): ErrorBody {
  if (isAppError(err)) {
    return fail(set, err);
  }
  console.error("[auth] unhandled", err);
  return failCode(set, ErrorCode.INTERNAL, 500);
}

/** Map thrown errors inside route handlers. */
export function handleRouteError(set: SetStatus, err: unknown): ErrorBody {
  return fromUnknown(set, err);
}
