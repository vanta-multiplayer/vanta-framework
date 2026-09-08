import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import path from "node:path";

/**
 * Issue a Let's Encrypt certificate for id.vantamp.xyz via HTTP-01 (webroot).
 *
 * Requirements:
 * - DNS A/AAAA for id.vantamp.xyz points at this host
 * - Ports 80/443 published
 * - Cloudflare: either DNS-only (grey cloud) during issue, OR orange-cloud with
 *   SSL mode that still forwards /.well-known/acme-challenge on HTTP
 * - CERTBOT_EMAIL set in .env
 *
 * Usage: bun run certs:http
 */

const root = path.join(import.meta.dir, "..");
const domain = process.env.CERTBOT_DOMAIN || "id.vantamp.xyz";
const webroot = path.join(root, "deploy", "nginx", "certbot");
const certDir = path.join(root, "deploy", "nginx", "certs");
const leLive = path.join(root, "deploy", "nginx", "letsencrypt", "live", domain);

function loadEnvEmail(): string {
  const fromEnv = process.env.CERTBOT_EMAIL?.trim();
  if (fromEnv) return fromEnv;
  const envPath = path.join(root, ".env");
  if (!existsSync(envPath)) {
    throw new Error("Set CERTBOT_EMAIL in .env (e.g. admin@vantamp.xyz)");
  }
  for (const line of readFileSync(envPath, "utf8").split(/\r?\n/)) {
    const m = /^CERTBOT_EMAIL=(.+)$/.exec(line.trim());
    if (m?.[1]) return m[1].trim();
  }
  throw new Error("Set CERTBOT_EMAIL in .env (e.g. admin@vantamp.xyz)");
}

mkdirSync(webroot, { recursive: true });
mkdirSync(certDir, { recursive: true });
mkdirSync(path.join(root, "deploy", "nginx", "letsencrypt"), { recursive: true });

const email = loadEnvEmail();
console.info(`[auth] requesting Let's Encrypt cert for ${domain} (HTTP-01) email=${email}`);

function preflightDns(): void {
  const check = spawnSync(
    "getent",
    ["hosts", "acme-v02.api.letsencrypt.org"],
    { encoding: "utf8" },
  );
  if (check.status !== 0) {
    const curl = spawnSync(
      "curl",
      ["-sS", "-o", "/dev/null", "-w", "%{http_code}", "--max-time", "10", "https://acme-v02.api.letsencrypt.org/directory"],
      { encoding: "utf8" },
    );
    if (curl.status !== 0) {
      console.error("[auth] host cannot resolve/reach acme-v02.api.letsencrypt.org");
      console.error("[auth] fix VPS DNS first, e.g. /etc/resolv.conf → nameserver 1.1.1.1");
      process.exit(1);
    }
  }
}

preflightDns();

const result = spawnSync(
  "docker",
  [
    "compose",
    "--profile",
    "certs",
    "run",
    "--rm",
    "certbot",
    "certonly",
    "--webroot",
    "-w",
    "/var/www/certbot",
    "-d",
    domain,
    "--email",
    email,
    "--agree-tos",
    "--no-eff-email",
    "--non-interactive",
    "--rsa-key-size",
    "2048",
  ],
  { cwd: root, encoding: "utf8", stdio: "inherit" },
);

if (result.status !== 0) {
  console.error("[auth] certbot failed");
  console.error("[auth] If the CA said 522: Cloudflare cannot reach this origin on :80.");
  console.error("[auth] Fix (pick one):");
  console.error("[auth]   1) Cloudflare DNS → grey-cloud (DNS only) for id.vantamp.xyz, open :80, retry");
  console.error("[auth]   2) Fix origin reachability (firewall/A-record/nginx), keep orange, retry");
  console.error("[auth]   3) Skip LE on origin: Cloudflare SSL Full + Origin CA (Epic sees CF's trusted cert)");
  process.exit(result.status ?? 1);
}

const fullchainSrc = path.join(leLive, "fullchain.pem");
const privkeySrc = path.join(leLive, "privkey.pem");
if (!existsSync(fullchainSrc) || !existsSync(privkeySrc)) {
  console.error(`[auth] expected certs under ${leLive}`);
  process.exit(1);
}

copyFileSync(fullchainSrc, path.join(certDir, "fullchain.pem"));
copyFileSync(privkeySrc, path.join(certDir, "privkey.pem"));
writeFileSync(
  path.join(certDir, "README.txt"),
  `Let's Encrypt cert for ${domain} (HTTP-01).\nRenew with: bun run certs:http\n`,
  "utf8",
);

console.info(`[auth] installed LE certs → ${certDir}`);
const reload = spawnSync("docker", ["compose", "exec", "-T", "nginx", "nginx", "-s", "reload"], {
  cwd: root,
  encoding: "utf8",
  stdio: "inherit",
});
if (reload.status !== 0) {
  console.warn("[auth] nginx reload failed — restart nginx: docker compose restart nginx");
} else {
  console.info("[auth] nginx reloaded with new certificate");
}
