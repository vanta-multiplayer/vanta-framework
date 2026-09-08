# Easy Anti-Cheat (EOS Anti-Cheat)

Vanta integrates **Epic Online Services Anti-Cheat** (Easy Anti-Cheat) in
client–server mode. The SDK lives under [`vendors/eos`](../vendors/eos/).

With the default `YOUR_*` placeholders in
[`code/shared/eos/credentials.h`](../code/shared/eos/credentials.h), EAC stays
**disabled** and the game runs normally.

## Where to get the IDs / secrets

1. Open the [Epic Games Developer Portal](https://dev.epicgames.com/portal).
2. Create or select your **organization** and **product** (VantaMP).
3. **Product Settings** → copy:
   - **Product ID** → `kProductId`
   - **Sandbox ID** → `kSandboxId`
   - **Deployment ID** (your live/dev deployment) → `kDeploymentId`
4. **Clients** → create both:
   - **GameClient** → paste into the `#ifdef VANTA_CLIENT` block
   - **TrustedServer** → paste into the `#ifdef VANTA_SERVER` block
5. Enable **Easy Anti-Cheat** for the product and activate a **client module**
   for your deployment. Download the integrity signing key/cert pair into
   [`vendors/eos/Keys`](../vendors/eos/Keys/) (`base_private.key` /
   `base_public.cer`).

Paste values into `credentials.h` or optional `eos.json` beside the client
(do not commit real secrets to a public repo).
CMake sets `VANTA_CLIENT` / `VANTA_SERVER` so each binary picks the right pair.
Connect on the client uses **silent Vanta OpenID → EOS Connect**
(`connect_provider=vanta`). There is no Epic Account / AccountPortal login.
See [eos.md](eos.md).

## SDK

Vendored as **EOS SDK 1.17.1.3** under [`vendors/eos`](../vendors/eos/) (Win32 + Win64
libs/dlls included). CMake defines `VANTAMP_HAS_EOS` when the matching `.lib` exists.

Anti-Cheat tools for this SDK: `vendors/eos/Tools/EOS_AntiCheatTools-win32-x64-2.2.0.zip`
(`anticheat_integritytool.exe` reports **v2.2.1**).

## Bootstrapper (runtime)

Real Anti-Cheat Client protection requires launching through
`start_protected_game.exe` from that tools zip’s `dist\`, **not**
`EOSBootstrapper.exe`.

Pack copies:

```
client\start_protected_game.exe
client\EasyAntiCheat\EasyAntiCheat_EOS_Setup.exe
client\EasyAntiCheat\Settings.json
client\EasyAntiCheat\SplashScreen.png
client\EasyAntiCheat\Localization\
```

The launcher restarts itself through `start_protected_game.exe` when needed
(parent process name check only — no `VANTA_EAC_*` environment variables).
Framework records **cwd** as the client runtime path, so the launcher pins cwd
to its exe directory before `Project` constructs. The client does not read
process environment variables for EAC.

First machine launch may prompt UAC while `EasyAntiCheat_EOS_Setup.exe` installs
the Windows service. Bootstrapper logs: `%AppData%\EasyAntiCheat\`.

## Integrity catalog (packaging)

For EOS SDK **1.17.1.3** / AntiCheatTools **2.2.x**, the protected build must be
catalogued and signed with `devtools\anticheat_integritytool.exe`. That tool
writes under the game root:

```
EasyAntiCheat\Certificates\base.bin
EasyAntiCheat\Certificates\runtime.conf
EasyAntiCheat\Certificates\base.cer
```

There is **no** requirement in this tool version to place a fabricated
`Something.exe.eac` next to the executable. Do not invent that layout from
other EAC eras.

### Generate (default mode)

From `anticheat_integritytool.exe --help`:

```text
integritytool -productid <eos_product_id>
  -inkey <base_private.key>
  -incert <base_public.cer>
  -outname base
  -target_game_dir <packed_client_root>
  <config_file>
```

Vanta wrapper (uses Keys + [`scripts/anticheat_integritytool.cfg`](../scripts/anticheat_integritytool.cfg)):

```bat
scripts\build.bat pack
scripts\build.bat protect
```

`protect` hashes the **client** pack into `base.bin`.

**EAX:** `gta_sa.exe` hard-imports `EAX.DLL`. We ship a **Vanta-built**
`eax.dll` (dsound forwarder under `code/client/host_stubs/eax`), not a copy of
the GTA install DLL. It sits next to the launcher so the IAT resolves there
instead of `gta-clean\eax.dll` (which EAC flags as Untrusted). Hashed in
`base.bin` like any other client binary.

**ogg/vorbis:** `gta_sa.exe` imports `vorbisfile.dll` (which loads `ogg.dll` /
`vorbis.dll`). Those are **built from Xiph libogg/libvorbis** into the client
tree (`host_stubs/vorbis`) — not retail GTA copies — and hashed in `base.bin`.

Do **not** MinHook `LoadLibrary*` to redirect those modules: after
`BeginSession` the AC client probes loader hooks and can kick with
`EOS_ACCCAR_InvalidMessage` (“Corrupt or invalid backend message”).

`EOS_AntiCheatClient_AddExternalIntegrityCatalog` returns `EOS_NotImplemented`
on this SDK/client module, so we do **not** use a separate `game.bin`.

Also supported: `protect_client.ps1 -Verify` / `-List`.

The authoritative requirement is that this catalog is produced and associated
with the configured **Deployment** as Epic expects for that product (Dev Portal
client module active; tool output present on the shipped client). Follow the
Portal / integrity-tool flow for your org — do not substitute unrelated file
formats.

### Private key

`base_private.key` is for **signing only**. Never ship it in player builds.
`protect_client.ps1` reads it from `vendors/eos/Keys/` and leaves only the
generated `Certificates\` outputs in `build\client`.

## Runtime flow

```text
Client: EOS_Initialize → Platform → Vanta Auth (/identity/token) → Connect (OPENID) → PUID
        → (on game connect, if identity EAC-eligible) BeginSession → EacRegister (ProtectedClient)
        → opaque EacOpaque ↔ server (server queues client opaques until RegisterClient)
Auth:   standalone services/auth (Postgres) — NOT game server :27016
Server: gameplay + EAC relay only
        → EOS_Initialize → Platform(bIsServer) → BeginSession
        → VerifyIdToken → RegisterClient (ProtectedClient) → flush queued opaques
        → ActionRequired / verify failure → kick
```

Epic Account Auth is not used. Device ID is not used for protected Anti-Cheat.

Per-packet EAC opaque `len=` / `crc=` traces are **debug** only (client + server).
Session start/stop and failures stay at info/error.

Note: an earlier post-join `C0000005` was misattributed to `BeginSession`; it was
a bad `ms_modelInfoPtrs` dereference in `SetVehiclePlateText` (fixed to match
MTA).

## Files

| Side | Path |
|------|------|
| Credentials | `code/shared/eos/credentials.h` |
| RPCs | `code/shared/rpc/eac_messages.h` |
| Client | `code/client/src/eac/` |
| Launcher | `code/launcher/src/eac_launch.*` |
| Server | `code/server/src/eac/` |
| Vendor | `vendors/eos/` (SDK **1.17.1.3**) |
| Tools zip | `vendors/eos/Tools/EOS_AntiCheatTools-win32-x64-*.zip` |
| Integrity cfg | `scripts/anticheat_integritytool.cfg` |
| Protect script | `scripts/protect_client.ps1` |
