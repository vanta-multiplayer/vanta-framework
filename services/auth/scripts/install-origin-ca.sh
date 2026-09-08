#!/usr/bin/env bash
# Install Cloudflare Origin CA certs for Full (strict).
# Usage:
#   bash scripts/install-origin-ca.sh ./cf-origin.pem ./cf-origin.key
# Then in Cloudflare: SSL/TLS → Full (strict)
set -euo pipefail
cd "$(dirname "$0")/.."

CERT_SRC="${1:-}"
KEY_SRC="${2:-}"

if [[ -z "$CERT_SRC" || -z "$KEY_SRC" ]]; then
  cat <<'EOF'
Cloudflare Origin CA → Full (strict)

1) Dashboard → SSL/TLS → Origin Server → Create certificate
   - Hostnames: id.vantamp.xyz (and optionally *.vantamp.xyz)
   - Validity: 15 years is fine
   - Key type: RSA (2048)
2) Save the cert + private key to two files (leave this terminal).
3) Run:
     bash scripts/install-origin-ca.sh /path/to/cert.pem /path/to/key.pem
4) Cloudflare SSL/TLS → Overview → Encryption mode: Full (strict)
5) Test: curl -sS https://id.vantamp.xyz/health
EOF
  exit 1
fi

if [[ ! -f "$CERT_SRC" || ! -f "$KEY_SRC" ]]; then
  echo "missing cert or key file" >&2
  exit 1
fi

mkdir -p deploy/nginx/certs
cp "$CERT_SRC" deploy/nginx/certs/fullchain.pem
cp "$KEY_SRC" deploy/nginx/certs/privkey.pem
chmod 644 deploy/nginx/certs/fullchain.pem
chmod 600 deploy/nginx/certs/privkey.pem

cat >deploy/nginx/certs/README.txt <<'EOF'
Cloudflare Origin CA certificate (for Full strict).
Not publicly trusted by browsers when hitting the origin IP directly — Cloudflare trusts it.
Renew/replace via: bash scripts/install-origin-ca.sh <cert.pem> <key.pem>
EOF

echo "installed Origin CA → deploy/nginx/certs/{fullchain,privkey}.pem"
if docker compose ps --status running 2>/dev/null | grep -q vanta-auth-nginx; then
  docker compose exec -T nginx nginx -t
  docker compose exec -T nginx nginx -s reload
  echo "nginx reloaded"
else
  echo "start nginx then: docker compose exec nginx nginx -s reload"
fi

echo "now set Cloudflare SSL mode to Full (strict)"
