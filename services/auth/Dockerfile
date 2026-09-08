# syntax=docker/dockerfile:1.7
FROM oven/bun:1.3-alpine

WORKDIR /app

# Keep install layer cacheable: only re-run when lockfile/manifest change.
COPY package.json bun.lock ./

# Fail fast if the build sandbox cannot reach the registry (common on VPS
# when Docker bridge DNS is broken). Compose sets `network: host` for this.
RUN --mount=type=cache,id=vanta-auth-bun-cache,target=/root/.bun/install/cache \
    sh -c 'wget -qO- --timeout=8 https://registry.npmjs.org/zod >/dev/null \
      || { echo "ERROR: no outbound HTTPS from build (registry.npmjs.org). On Linux VPS use compose build.network=host or fix Docker DNS."; exit 1; }' \
    && bun install --frozen-lockfile --production --ignore-scripts

COPY tsconfig.json ./
COPY src ./src

ENV NODE_ENV=production
ENV VANTA_AUTH_HOST=0.0.0.0
ENV VANTA_AUTH_PORT=28080

EXPOSE 28080

CMD ["bun", "run", "src/index.ts"]
