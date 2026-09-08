export const ErrorCode = {
  NOT_FOUND: "not_found",
  INVALID_BODY: "invalid_body",
  INVALID_CREDENTIALS: "invalid_install_credentials",
  RATE_LIMITED: "rate_limited",
  UNAUTHORIZED: "unauthorized",
  INSTALLATION_AUTH_FAILED: "installation_auth_failed",
  ACCOUNT_INACTIVE: "account_inactive",
  MISSING_BEARER: "missing_bearer",
  INVALID_TOKEN: "invalid_token",
  INTERNAL: "internal_error",
} as const;

export type ErrorCode = (typeof ErrorCode)[keyof typeof ErrorCode];

const MESSAGES: Record<ErrorCode, string> = {
  [ErrorCode.NOT_FOUND]: "Resource not found",
  [ErrorCode.INVALID_BODY]: "Request body is invalid",
  [ErrorCode.INVALID_CREDENTIALS]: "Installation credentials are invalid",
  [ErrorCode.RATE_LIMITED]: "Too many requests",
  [ErrorCode.UNAUTHORIZED]: "Unauthorized",
  [ErrorCode.INSTALLATION_AUTH_FAILED]: "Installation authentication failed",
  [ErrorCode.ACCOUNT_INACTIVE]: "Account is inactive",
  [ErrorCode.MISSING_BEARER]: "Missing Authorization Bearer token",
  [ErrorCode.INVALID_TOKEN]: "Access token is invalid or expired",
  [ErrorCode.INTERNAL]: "Internal server error",
};

export class AppError extends Error {
  readonly code: ErrorCode;
  readonly status: number;
  readonly details?: unknown;

  constructor(code: ErrorCode, status: number, details?: unknown) {
    super(MESSAGES[code] ?? code);
    this.name = "AppError";
    this.code = code;
    this.status = status;
    this.details = details;
  }
}

export function isAppError(value: unknown): value is AppError {
  return value instanceof AppError;
}
