# Vanta Auth Backend

Standalone identity service for silent Vanta OpenID → EOS Connect.

**Public URL:** `https://id.vantamp.xyz`

## Quick start (full Docker)

Fresh wipe + secrets + certs + compose (destroys DB volumes):

```bash
cd services/auth   # or ~/vanta_mp_id on the VPS
bash scripts/setup-fresh.sh --yes
# same as: bun run setup:fresh
```

Manual path:

```bash
bun run secrets:gen
bun run certs:dev
DOCKER_BUILDKIT=1 docker compose up -d --build
```

TLS for Cloudflare **Full (strict)** (pick one):

```bash
bun run certs:http                              # Let's Encrypt (needs :80 reachable)
# or
bash scripts/install-origin-ca.sh cert.pem key.pem   # Cloudflare Origin CA
```

Then set Cloudflare SSL mode to **Full (strict)**. Self-signed + **Full** is enough for Epic (edge cert is Cloudflare’s).

### `password authentication failed for user "vanta"`

Postgres stores the password **inside the volume on first boot**. Regenerating `.env` / `secrets:gen --rotate` does not update it.

Wipe the DB volume (destroys auth DB data — fine for a fresh IdP):

```bash
docker compose down
docker volume ls | grep vanta_auth_pg
docker volume rm <project>_vanta_auth_pg    # exact name from ls
docker compose up -d
```

`bun run secrets:gen` now **keeps** existing passwords unless you pass `--rotate`.

### Slow `bun install` on a small VPS?

Cold npm/bun downloads on a tiny VPS are often just bandwidth. Prefer:

**A) Build on your PC, ship the image (fastest)**

```bash
# on your fast machine, in services/auth:
bun run image:export
scp vanta-auth-app.tar.gz user@vps:/tmp/

# on the VPS:
gunzip -c /tmp/vanta-auth-app.tar.gz | docker load
cd /path/to/services/auth
docker compose up -d --no-build   # uses vanta-auth-app:latest
```

**B) Keep BuildKit cache on the VPS** (rebuilds after the first are much quicker)

```bash
export DOCKER_BUILDKIT=1
docker compose build auth   # first time still slow; later uses cache mounts
```

**C) Build networking looks dead (5+ min for ~5 deps)**

Docker’s default **bridge DNS** on many VPS images is broken or glacial. This repo sets `build.network: host` for `auth` so `bun install` uses the host’s network.

Confirm on the VPS:

```bash
bash scripts/diagnose-docker-net.sh
```

If bridge fails and host works, you’re done (compose already uses host for builds). If both fail, the VPS itself has no egress. Optional permanent DNS fix:

```json
// /etc/docker/daemon.json
{ "dns": ["1.1.1.1", "8.8.8.8"] }
```

then `sudo systemctl restart docker`.

Note: the **running** `auth` container only joins the private `auth_net` (no public ports) — that’s intentional; nginx fronts it. Build-time egress is separate from runtime.

## Stack

| Service | Role |
|---------|------|
| **nginx** | TLS `:443`, ACME on `:80`, Cloudflare real-IP restore |
| **auth** | Bun API (private) |
| **postgres** | Source of truth |
| **redis** | UserInfo cache |
| **certbot** | `bun run certs:http` (profile `certs`) |

## Cloudflare

Orange-cloud in front of this host is expected.

- nginx trusts Cloudflare proxy ranges (`deploy/nginx/dynamic/cloudflare-realip.conf`)
- Ranges auto-refresh every **3 hours** (best-effort; seed file is enough if egress is broken)
- Visitor IP from `CF-Connecting-IP`
- **Recommended TLS:** Cloudflare SSL **Full (strict)** + [Origin CA](https://developers.cloudflare.com/ssl/origin-configuration/origin-ca/) on the VPS:

```bash
# create cert in CF dashboard (SSL/TLS → Origin Server), save pem+key, then:
bash scripts/install-origin-ca.sh ./cf-origin.pem ./cf-origin.key
# Cloudflare → SSL/TLS → Full (strict)
curl -sS https://id.vantamp.xyz/health
```

(Self-signed + **Full** also works; Origin CA is required for **Full strict**.)
- **522 with correct A record:** not nginx (local `/health` = 200). Inbound TCP to the VPS is blocked (provider firewall / iptables). Fix panel firewall **or** use a Tunnel (orange stays on):

```bash
# prove inbound (from your PC — NOT the VPS):
curl -sk --connect-timeout 5 https://31.214.141.58/health -H 'Host: id.vantamp.xyz'
# timeout ⇒ provider/host firewall, not DNS/nginx

# Tunnel workaround (Zero Trust → Tunnels → create → token):
# Public hostname: id.vantamp.xyz → http://127.0.0.1:80
echo 'CLOUDFLARE_TUNNEL_TOKEN=eyJ...' >> .env
docker compose --profile tunnel up -d cloudflared
```

| Command | Purpose |
|---------|---------|
| `bun run certs:dev` | Self-signed bootstrap |
| `bun run certs:http` | Optional Let’s Encrypt on origin (HTTP-01) |

Set `CERTBOT_EMAIL` in `.env` before `certs:http`.

Epic OpenID UserInfo (must be trusted HTTPS):

```
https://id.vantamp.xyz/eos/userinfo
```

Client `eos.json`:

```json
{ "connect_provider": "vanta", "auth_service_url": "https://id.vantamp.xyz" }
```
