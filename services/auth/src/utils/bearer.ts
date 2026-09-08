/** Parse `Authorization: Bearer <token>` strictly. */
export function extractBearer(authorization: string | null | undefined): string | null {
  if (!authorization) return null;
  const match = /^Bearer\s+(\S+)\s*$/i.exec(authorization.trim());
  return match?.[1] ?? null;
}
