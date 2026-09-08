import { createHash, randomBytes } from "node:crypto";

/** Opaque bearer token for EOS Connect / UserInfo (never store plaintext). */
export function mintOpaqueToken(byteCount = 32): string {
  return randomBytes(byteCount).toString("base64url");
}

/** Stable server-owned account / installation / session ids. */
export function mintId(byteCount = 16): string {
  return randomBytes(byteCount).toString("hex");
}

export function sha256Hex(value: string): string {
  return createHash("sha256").update(value, "utf8").digest("hex");
}

/** Hash long-lived installation secrets (Argon2id via Bun). */
export async function hashCredential(secret: string): Promise<string> {
  return Bun.password.hash(secret, {
    algorithm: "argon2id",
    memoryCost: 19456,
    timeCost: 2,
  });
}

export async function verifyCredential(secret: string, hash: string): Promise<boolean> {
  try {
    return await Bun.password.verify(secret, hash);
  } catch {
    return false;
  }
}
