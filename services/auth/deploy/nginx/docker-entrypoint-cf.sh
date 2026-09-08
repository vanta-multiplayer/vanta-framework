#!/bin/sh
# nginx entrypoint wrapper: seed Cloudflare real_ip ranges, start nginx, refresh in background.
set -eu

INTERVAL="${CLOUDFLARE_IP_REFRESH_SECONDS:-10800}" # 3 hours
export CLOUDFLARE_REALIP_PATH="${CLOUDFLARE_REALIP_PATH:-/etc/nginx/dynamic/cloudflare-realip.conf}"
SEED="/etc/nginx/cloudflare-realip.seed.conf"
REFRESH="/usr/local/bin/refresh-cloudflare-ips.sh"

mkdir -p "$(dirname "$CLOUDFLARE_REALIP_PATH")"

# Always prefer a local seed so nginx never depends on outbound HTTPS at boot.
# (Many VPS Docker bridges cannot reach the internet / Cloudflare.)
if [ -f "$SEED" ]; then
  if [ ! -s "$CLOUDFLARE_REALIP_PATH" ]; then
    cp "$SEED" "$CLOUDFLARE_REALIP_PATH"
    echo "cloudflare-ips: seeded ${CLOUDFLARE_REALIP_PATH}"
  fi
fi

if [ ! -s "$CLOUDFLARE_REALIP_PATH" ]; then
  echo "cloudflare-ips: ERROR missing real_ip config at ${CLOUDFLARE_REALIP_PATH}" >&2
  exit 1
fi

# Refresh in background only — never block nginx startup on wget timeouts.
(
  # Short delay so nginx is already listening before first attempt.
  sleep 2
  if [ -f "$REFRESH" ]; then
    /bin/sh "$REFRESH" || echo "cloudflare-ips: refresh failed (using seeded ranges; ok)"
  fi
  while true; do
    sleep "$INTERVAL"
    /bin/sh "$REFRESH" || true
  done
) &

echo "cloudflare-ips: using ${CLOUDFLARE_REALIP_PATH}; auto-refresh every ${INTERVAL}s (best-effort)"

exec /docker-entrypoint.sh "$@"
