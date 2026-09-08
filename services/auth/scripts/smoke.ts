const base =
  process.env.SMOKE_BASE_URL?.replace(/\/$/, "") ||
  process.env.VANTA_AUTH_PUBLIC_URL?.replace(/\/$/, "") ||
  "https://id.vantamp.xyz";

// Allow self-signed local certs when explicitly requested.
if (process.env.SMOKE_INSECURE === "1") {
  process.env.NODE_TLS_REJECT_UNAUTHORIZED = "0";
}
const installationId = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const installationSecret = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

function unwrap(body: Record<string, unknown>) {
  if (body && typeof body === "object" && body.ok === true && body.data && typeof body.data === "object") {
    return body.data as Record<string, unknown>;
  }
  return body;
}

async function main() {
  const health = await fetch(`${base}/health`);
  const healthBody = await health.json();
  console.log("health", health.status, healthBody);

  const reg = await fetch(`${base}/identity/register`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ installationId, installationSecret, displayName: "DevPlayer" }),
  });
  const regBody = await reg.json();
  console.log("register", reg.status, unwrap(regBody));

  const tok = await fetch(`${base}/identity/token`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ installationId, installationSecret, displayName: "DevPlayer" }),
  });
  const tokBody = await tok.json();
  const tokData = unwrap(tokBody);
  console.log("token", tok.status, {
    expiresIn: tokData.expiresIn,
    accountId: tokData.accountId,
    accessTokenLen: typeof tokData.accessToken === "string" ? tokData.accessToken.length : 0,
  });

  const info = await fetch(`${base}/eos/userinfo`, {
    headers: { authorization: `Bearer ${tokData.accessToken}` },
  });
  const infoBody = await info.json();
  console.log("userinfo", info.status, infoBody);

  const tok2 = await fetch(`${base}/identity/token`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ installationId, installationSecret }),
  });
  const tok2Body = await tok2.json();
  const tok2Data = unwrap(tok2Body);
  const info2 = await fetch(`${base}/eos/userinfo`, {
    headers: { authorization: `Bearer ${tok2Data.accessToken}` },
  });
  const info2Body = await info2.json();
  console.log("stable_sub", infoBody.sub === info2Body.sub, info2Body.sub);
}

await main();
