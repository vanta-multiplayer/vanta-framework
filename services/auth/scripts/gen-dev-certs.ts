import { existsSync, mkdirSync, unlinkSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import path from "node:path";

const root = path.join(import.meta.dir, "..");
const certDir = path.join(root, "deploy", "nginx", "certs");
const fullchain = path.join(certDir, "fullchain.pem");
const privkey = path.join(certDir, "privkey.pem");
const domain = "id.vantamp.xyz";
const force = process.argv.includes("--force");

mkdirSync(certDir, { recursive: true });
mkdirSync(path.join(root, "deploy", "nginx", "certbot"), { recursive: true });

if (force) {
  for (const p of [fullchain, privkey]) {
    if (existsSync(p)) unlinkSync(p);
  }
}

if (existsSync(fullchain) && existsSync(privkey)) {
  console.info(`[auth] TLS certs already present in ${certDir}`);
  console.info("[auth] Replace with Let's Encrypt / real certs for production");
  process.exit(0);
}

function tryOpenssl(bin: string): boolean {
  const result = spawnSync(
    bin,
    [
      "req",
      "-x509",
      "-nodes",
      "-newkey",
      "rsa:2048",
      "-days",
      "825",
      "-keyout",
      privkey,
      "-out",
      fullchain,
      "-subj",
      `/CN=${domain}`,
      "-addext",
      `subjectAltName=DNS:${domain},DNS:localhost,IP:127.0.0.1`,
    ],
    { encoding: "utf8" },
  );
  return result.status === 0;
}

const opensslCandidates = [
  "openssl",
  "C:\\Program Files\\Git\\usr\\bin\\openssl.exe",
  "C:\\Program Files\\OpenSSL-Win64\\bin\\openssl.exe",
];

let ok = false;
for (const bin of opensslCandidates) {
  if (tryOpenssl(bin)) {
    ok = true;
    break;
  }
}

if (!ok) {
  // Fallback: generate via Docker (alpine/openssl) without requiring a host OpenSSL.
  const docker = spawnSync(
    "docker",
    [
      "run",
      "--rm",
      "-v",
      `${certDir}:/certs`,
      "alpine/openssl",
      "req",
      "-x509",
      "-nodes",
      "-newkey",
      "rsa:2048",
      "-days",
      "825",
      "-keyout",
      "/certs/privkey.pem",
      "-out",
      "/certs/fullchain.pem",
      "-subj",
      `/CN=${domain}`,
      "-addext",
      `subjectAltName=DNS:${domain},DNS:localhost,IP:127.0.0.1`,
    ],
    { encoding: "utf8" },
  );
  if (docker.status !== 0) {
    console.error(docker.stderr || docker.stdout || "cert generation failed");
    console.error("[auth] Place fullchain.pem + privkey.pem under deploy/nginx/certs/");
    process.exit(1);
  }
}

writeFileSync(
  path.join(certDir, "README.txt"),
  `Self-signed cert for ${domain} (dev only).\nReplace with Let's Encrypt for production.\n`,
  "utf8",
);
console.info(`[auth] wrote self-signed certs for ${domain} → ${certDir}`);
console.info("[auth] browsers will warn; Epic UserInfo needs a publicly trusted cert in production");
