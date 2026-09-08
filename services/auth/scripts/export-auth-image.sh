#!/usr/bin/env bash
# Build auth image on a fast machine, write a gzipped tarball for the VPS.
set -euo pipefail
cd "$(dirname "$0")/.."

export DOCKER_BUILDKIT=1
IMAGE="${AUTH_IMAGE_NAME:-vanta-auth-app:latest}"
OUT="${1:-./vanta-auth-app.tar.gz}"

docker compose build auth
docker save "$IMAGE" | gzip -1 >"$OUT"

echo
echo "Wrote $OUT"
echo "On the VPS:"
echo "  gunzip -c $(basename "$OUT") | docker load"
echo "  AUTH_IMAGE_NAME=$IMAGE docker compose up -d --no-build"
