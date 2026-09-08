# Epic Online Services (EOS) / Easy Anti-Cheat

Vendored from the Epic Anti-Cheat / EOS SDK drop for VantaMP.

**SDK version: 1.17.1.3** (from `easyanticheat-sdk/SDK`).

| Path | Contents |
|------|----------|
| `Include/` | EOS public headers |
| `Lib/Win64/` | `EOSSDK-Win64-Shipping.lib` — **server** |
| `Bin/Win64/` | `EOSSDK-Win64-Shipping.dll` — copy next to `VantaMPServer.exe` |
| `Lib/Win32/` | `EOSSDK-Win32-Shipping.lib` — **client** (32-bit) |
| `Bin/Win32/` | `EOSSDK-Win32-Shipping.dll` |
| `Tools/` | Bootstrapper, Anti-Cheat tools zips, DevAuthTool |
| `Keys/` | EAC integrity cert (`base_public.cer`) + private key (`base_private.key`) |

Product credentials: [`code/shared/eos/credentials.h`](../../code/shared/eos/credentials.h). See [`docs/eac.md`](../../docs/eac.md).

`Keys/` stays in-repo for signing the integrity catalog with
`anticheat_integritytool.exe` (AntiCheatTools 2.2.x for this SDK).
Do **not** ship `base_private.key` in player builds — only
`EasyAntiCheat/Certificates/` (`base.bin`, `runtime.conf`, `base.cer`) from the
integrity tool goes next to the game. See [`docs/eac.md`](../../docs/eac.md).
