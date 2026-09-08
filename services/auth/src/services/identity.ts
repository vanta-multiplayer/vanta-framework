import type { AuthConfig } from "../config";
import { cacheUserInfo, getCachedUserInfo } from "../cache/userinfo";
import { hashCredential, mintOpaqueToken, sha256Hex, verifyCredential } from "../crypto/tokens";
import type { Database } from "../db";
import * as accountsRepo from "../repositories/accounts";
import * as installationsRepo from "../repositories/installations";
import * as sessionsRepo from "../repositories/sessions";
import { AppError, ErrorCode } from "../utils/errors";

const INSTALL_ID_RE = /^[a-f0-9]{16,128}$/i;
const SECRET_MIN = 32;
const SECRET_MAX = 256;

export function validateInstallationId(id: string): boolean {
  return INSTALL_ID_RE.test(id);
}

export function validateSecret(secret: string): boolean {
  return secret.length >= SECRET_MIN && secret.length <= SECRET_MAX;
}

export async function registerInstallation(
  db: Database,
  installationId: string,
  installationSecret: string,
  displayName?: string,
): Promise<{ accountId: string; installationId: string; created: boolean }> {
  const existing = await installationsRepo.findActiveInstallation(db, installationId);
  if (existing) {
    const ok = await verifyCredential(installationSecret, existing.credentialHash);
    if (!ok) {
      throw new AppError(ErrorCode.INSTALLATION_AUTH_FAILED, 401);
    }
    if (displayName) {
      await accountsRepo.updateDisplayName(db, existing.accountId, displayName);
    }
    return { accountId: existing.accountId, installationId: existing.id, created: false };
  }

  const account = await accountsRepo.createAccount(db, displayName ?? "");
  const credentialHash = await hashCredential(installationSecret);
  await installationsRepo.insertInstallation(db, installationId, account.id, credentialHash);
  console.info(`[auth] provisioned account (id length=${account.id.length}) for new installation`);
  return { accountId: account.id, installationId, created: true };
}

export async function issueAccessToken(
  db: Database,
  config: AuthConfig,
  installationId: string,
  installationSecret: string,
  displayName?: string,
): Promise<{ accessToken: string; expiresIn: number; accountId: string }> {
  const installation = await installationsRepo.findActiveInstallation(db, installationId);
  if (!installation) {
    throw new AppError(ErrorCode.UNAUTHORIZED, 401);
  }

  const ok = await verifyCredential(installationSecret, installation.credentialHash);
  if (!ok) {
    throw new AppError(ErrorCode.INSTALLATION_AUTH_FAILED, 401);
  }

  const account = await accountsRepo.getActiveAccount(db, installation.accountId);
  if (!account) {
    throw new AppError(ErrorCode.ACCOUNT_INACTIVE, 401);
  }

  if (displayName) {
    await accountsRepo.updateDisplayName(db, account.id, displayName);
  }

  await installationsRepo.touchInstallation(db, installation.id);

  const accessToken = mintOpaqueToken(32);
  const tokenHash = sha256Hex(accessToken);
  const expiresIn = config.accessTokenTtlSeconds;
  const expiresAt = new Date(Date.now() + expiresIn * 1000);
  await sessionsRepo.createSession(db, account.id, tokenHash, expiresAt);

  // Warm UserInfo cache for Epic's first Connect validation hop.
  await cacheUserInfo(
    tokenHash,
    { sub: account.id, name: account.displayName },
    expiresIn,
  );

  console.info(
    `[auth] issued access token for account (id length=${account.id.length}) expires_in=${expiresIn}s`,
  );

  return { accessToken, expiresIn, accountId: account.id };
}

export async function resolveUserInfo(
  db: Database,
  bearerToken: string,
): Promise<{ sub: string; name: string } | null> {
  const tokenHash = sha256Hex(bearerToken);

  const cached = await getCachedUserInfo(tokenHash);
  if (cached) {
    return cached;
  }

  const session = await sessionsRepo.findValidSessionByTokenHash(db, tokenHash);
  if (!session) {
    return null;
  }
  const account = await accountsRepo.getActiveAccount(db, session.accountId);
  if (!account) {
    return null;
  }

  const info = { sub: account.id, name: account.displayName };
  const ttlSeconds = Math.max(
    1,
    Math.floor((session.expiresAt.getTime() - Date.now()) / 1000),
  );
  await cacheUserInfo(tokenHash, info, ttlSeconds);
  return info;
}
