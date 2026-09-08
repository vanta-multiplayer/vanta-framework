#!/usr/bin/env bash
# Compare Docker bridge vs host network egress (run on the VPS).
set -euo pipefail
IMG="${1:-oven/bun:1.3-alpine}"

echo "== bridge (default) =="
docker run --rm "$IMG" sh -c 'time wget -qO- --timeout=10 https://registry.npmjs.org/zod >/dev/null && echo OK || echo FAIL'

echo
echo "== host network =="
docker run --rm --network=host "$IMG" sh -c 'time wget -qO- --timeout=10 https://registry.npmjs.org/zod >/dev/null && echo OK || echo FAIL'

echo
echo "If bridge is slow/FAIL and host is fast/OK: Docker DNS/bridge is the problem."
echo "Compose already sets build.network=host for auth. Optional daemon fix:"
echo '  {"dns":["1.1.1.1","8.8.8.8"]}' 
echo "  -> /etc/docker/daemon.json then: systemctl restart docker"
