# EOS / EAC identity notes

## UX goal

```
launch VantaMP → silent Vanta Auth → EOS Connect → EAC → play
```

No Epic Account login, AccountPortal, PersistentAuth, overlay, or browser prompt.

## Architecture

```
                    ┌────────────────────────────┐
                    │  Vanta Auth Backend        │
                    │  services/auth (Bun)       │
                    │  https://id.vantamp.xyz    │
                    │  (nginx + app + pg + redis)│
                    └────────────┬───────────────┘
                                 │
           ┌─────────────────────┼─────────────────────┐
           │                     │                     │
           ▼                     ▼                     ▼
      Vanta Launcher         Game Client          Epic EOS Backend
                                                 GET /eos/userinfo

Vanta Game Server (gameplay UDP / HTTP for non-identity)
  → sync, resources, voice, EAC opaque relay
  → NOT the identity authority
```

## Identity flow

```
%AppData%\VantaMP\install.json  { installationId, secret }
        |
        v
POST https://id.vantamp.xyz/identity/register   (idempotent)
POST https://id.vantamp.xyz/identity/token      → opaque accessToken
        |
        v
EOS_Connect_Login(EOS_ECT_OPENID_ACCESS_TOKEN, UserLoginInfo=NULL)
        |
        +→ Success | InvalidUser → CreateUser (first time only)
        v
EOS_ProductUserId_IsValid → join game → EAC BeginSession
```

`sub` is minted only by the auth backend and returned via UserInfo. Access-token
refresh never changes `sub`.

Verified end-to-end: silent OpenID → Connect PUID → server join → EAC s↔c traffic.

## Client `eos.json`

Optional beside the client binary (defaults already point at production):

```json
{
  "connect_provider": "vanta",
  "auth_service_url": "https://id.vantamp.xyz"
}
```

(`auth_url` is accepted as an alias.)

## Auth backend deploy

See [services/auth/README.md](../services/auth/README.md).

```bash
cd services/auth
bash scripts/setup-fresh.sh --yes
# Cloudflare SSL: Full, or Full (strict) with LE / Origin CA
```

## Epic Developer Portal — OpenID IdP (manual)

Portal: https://dev.epicgames.com/portal → product → **Product Settings** → **Identity Providers** → **OpenID**

Docs: https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/identity-provider-management

| Portal field | Value |
|--------------|--------|
| Identity Provider | **OpenID** |
| Type | **UserInfo** (not JWKS) |
| Description | e.g. `Vanta` |
| **UserInfo API Endpoint** | `https://id.vantamp.xyz/eos/userinfo` |
| **HTTP method** | **GET** |
| **AccountId field** | `sub` |
| **DisplayName field** | `name` |

Then:

1. Enable that OpenID provider on the **same Sandbox** as `sandbox_id` / credentials (`credentials.h` / `eos.json`).
2. Keep Product / Sandbox / Deployment aligned with GameClient credentials.
3. Client policy must allow Connect + OpenID credential type.
4. UserInfo must be **public HTTPS** reachable by Epic (orange-cloud Cloudflare is fine; Epic sees Cloudflare’s edge cert).

`openid configuration not found for sandboxId` → IdP not enabled on that sandbox.

## TLS (Cloudflare)

Epic and browsers talk to Cloudflare. Origin can use:

| Origin cert | Cloudflare mode |
|-------------|-----------------|
| Self-signed (`certs:dev`) | **Full** |
| Let’s Encrypt (`certs:http`) | **Full (strict)** |
| Cloudflare Origin CA | **Full (strict)** |

Do not require grey-cloud for Epic. Prefer keeping `id.vantamp.xyz` orange-proxied.

## What was removed

- In-process OpenID issuer on the game server (`POST /identity/token` on `:27016`)
- Epic Auth / AccountPortal / PersistentAuth / DeviceID for EAC
- Client-trusted account ids / self-signed JWTs
