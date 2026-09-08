# Pack a redistributable client + server tree (no CMake, PDBs, cache, or logs).
# Default dest: <workspace>/build
param(
    [string]$Dest = "",
    [string]$ClientBin = "",
    [string]$ServerBin = ""
)

$ErrorActionPreference = "Stop"

$VantaRoot = Split-Path -Parent $PSScriptRoot
$FrameworkRoot = (Resolve-Path (Join-Path $VantaRoot "..\..\..")).Path
$WorkspaceRoot = (Resolve-Path (Join-Path $FrameworkRoot "..")).Path

if (-not $Dest) { $Dest = Join-Path $WorkspaceRoot "build" }
if (-not $ClientBin) { $ClientBin = Join-Path $FrameworkRoot "builds\build\bin" }
if (-not $ServerBin) { $ServerBin = Join-Path $FrameworkRoot "builds\build-64\bin" }

foreach ($path in @($ClientBin, $ServerBin)) {
    if (-not (Test-Path $path)) {
        throw "Missing build output: $path`nBuild VantaMPClient (32) and VantaMPServer (64) first."
    }
}

$ClientOut = Join-Path $Dest "client"
$ServerOut = Join-Path $Dest "server"

if (Test-Path $Dest) {
    Remove-Item $Dest -Recurse -Force
}
New-Item -ItemType Directory -Path $ClientOut, $ServerOut | Out-Null

$ClientFiles = @(
    "VantaMPLauncher.exe",
    "VantaMPClient.dll",
    "FrameworkLoaderData.dll",
    "eax.dll",
    "ogg.dll",
    "vorbis.dll",
    "vorbisFile.dll",
    "cef_subprocess.exe",
    "libcef.dll",
    "chrome_elf.dll",
    "chrome_100_percent.pak",
    "chrome_200_percent.pak",
    "resources.pak",
    "icudtl.dat",
    "v8_context_snapshot.bin",
    "libEGL.dll",
    "libGLESv2.dll",
    "d3dcompiler_47.dll",
    "vk_swiftshader.dll",
    "vk_swiftshader_icd.json",
    "vulkan-1.dll",
    "crashpad_handler.exe",
    "crashpad_wer.dll",
    "libcrypto-3.dll",
    "libssl-3.dll",
    "discord_game_sdk.dll",
    "fw_steam_api.dll"
)

foreach ($name in $ClientFiles) {
    $src = Join-Path $ClientBin $name
    if (-not (Test-Path $src)) {
        throw "Missing client file: $src"
    }
    Copy-Item $src $ClientOut
}

Copy-Item (Join-Path $ClientBin "locales") (Join-Path $ClientOut "locales") -Recurse

# EOS / Easy Anti-Cheat runtime (client = Win32)
$EosRoot = Join-Path $VantaRoot "vendors\eos"
$EosClientDll = Join-Path $EosRoot "Bin\Win32\EOSSDK-Win32-Shipping.dll"
if (Test-Path $EosClientDll) {
    Copy-Item $EosClientDll $ClientOut
} elseif (Test-Path (Join-Path $ClientBin "EOSSDK-Win32-Shipping.dll")) {
    Copy-Item (Join-Path $ClientBin "EOSSDK-Win32-Shipping.dll") $ClientOut
}

# Anti-Cheat bootstrapper layout (start_protected_game.exe + EasyAntiCheat\).
# EOSBootstrapper.exe is NOT the Anti-Cheat launcher — do not use it for EAC.
$EosTools = Join-Path $EosRoot "Tools"
$AcZip = Get-ChildItem -Path $EosTools -Filter "EOS_AntiCheatTools-win32-x64-*.zip" -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    Select-Object -First 1

if ($AcZip) {
    $AcExtract = Join-Path $env:TEMP ("vanta-eac-tools-" + [guid]::NewGuid().ToString("N"))
    try {
        Expand-Archive -LiteralPath $AcZip.FullName -DestinationPath $AcExtract -Force
        $Dist = Join-Path $AcExtract "dist"
        if (-not (Test-Path $Dist)) {
            throw "Anti-Cheat tools zip missing dist\: $($AcZip.FullName)"
        }

        Copy-Item (Join-Path $Dist "start_protected_game.exe") $ClientOut -Force
        Copy-Item (Join-Path $Dist "EasyAntiCheat") (Join-Path $ClientOut "EasyAntiCheat") -Recurse -Force

        function Get-EosCredentialConst([string]$Name) {
            $Creds = Join-Path $VantaRoot "code\shared\eos\credentials.h"
            $pattern = "k$Name\s*=\s*`"([^`"]+)`""
            $m = Select-String -Path $Creds -Pattern $pattern | Select-Object -First 1
            if (-not $m) { throw "Could not parse k$Name from credentials.h" }
            return $m.Matches.Groups[1].Value
        }

        $productId    = Get-EosCredentialConst "ProductId"
        $sandboxId    = Get-EosCredentialConst "SandboxId"
        $deploymentId = Get-EosCredentialConst "DeploymentId"

        # Match dist\EasyAntiCheat\Settings.json field names/types from AntiCheatTools 2.2.x.
        $settingsJson = @"
{
	"title"											: "VantaMP",
	"executable"									: "VantaMPLauncher.exe",
	"productid"										: "$productId",
	"sandboxid"										: "$sandboxId",
	"deploymentid"									: "$deploymentId",
	"requested_splash"								: "EasyAntiCheat/SplashScreen.png",
	"wait_for_game_process_exit"					: "true",
	"hide_bootstrapper"								: "false",
	"hide_gui"										: "false"
}
"@
        Set-Content -Path (Join-Path $ClientOut "EasyAntiCheat\Settings.json") -Value $settingsJson -Encoding utf8
        Write-Host "EAC: packed start_protected_game + Settings.json from $($AcZip.Name)"
        Write-Host "EAC: after pack, run scripts\protect_client.ps1 for base.bin."
    }
    finally {
        if (Test-Path $AcExtract) {
            Remove-Item $AcExtract -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
else {
    Write-Warning "No EOS_AntiCheatTools-win32-x64-*.zip under vendors\eos\Tools - Anti-Cheat bootstrapper not packed."
}

@'
{
    "display_adapter": 0,
    "display_borderless": false,
    "display_exclusive": false,
    "display_height": 0,
    "display_setup_done": false,
    "display_width": 0,
    "frame_limit": 60,
    "volume_master": 1.0,
    "volume_sfx": 1.0,
    "volume_radio": 1.0,
    "voice_input_device": "default",
    "voice_output_device": "default",
    "voice_volume": 1.0,
    "voice_ptt_key": 86,
    "nickname": "Player",
    "server_host": "127.0.0.1",
    "server_port": 27015,
    "netgraph": false
}
'@ | Set-Content -Path (Join-Path $ClientOut "VantaMP.json") -Encoding ascii

@'
{
    "game_executable_name": "gta_sa.exe",
    "game_path_manual": false,
    "mod_dll_name": "VantaMPClient.dll",
    "steam_app_id": 430
}
'@ | Set-Content -Path (Join-Path $ClientOut "VantaMP_launcher.json") -Encoding ascii

$ServerFiles = @(
    "VantaMPServer.exe",
    "libnode.dll",
    "libcrypto-3-x64.dll",
    "libssl-3-x64.dll",
    "crashpad_handler.exe",
    "crashpad_wer.dll"
)

foreach ($name in $ServerFiles) {
    $src = Join-Path $ServerBin $name
    if (-not (Test-Path $src)) {
        throw "Missing server file: $src"
    }
    Copy-Item $src $ServerOut
}

$EosServerDll = Join-Path $EosRoot "Bin\Win64\EOSSDK-Win64-Shipping.dll"
if (Test-Path $EosServerDll) {
    Copy-Item $EosServerDll $ServerOut
} elseif (Test-Path (Join-Path $ServerBin "EOSSDK-Win64-Shipping.dll")) {
    Copy-Item (Join-Path $ServerBin "EOSSDK-Win64-Shipping.dll") $ServerOut
}

Copy-Item (Join-Path $ServerBin "resources") (Join-Path $ServerOut "resources") -Recurse

$catalogSrc = Join-Path $ServerBin "catalog"
if (-not (Test-Path $catalogSrc)) {
    throw "Missing server catalog: $catalogSrc`nRebuild VantaMPServer so CMake copies data/catalog next to the exe."
}
Copy-Item $catalogSrc (Join-Path $ServerOut "catalog") -Recurse

@'
{
    "host": "0.0.0.0",
    "map": "",
    "maxplayers": 32,
    "mod": {},
    "port": 27015,
    "server-token": ""
}
'@ | Set-Content -Path (Join-Path $ServerOut "server.json") -Encoding ascii

@'
VantaMP test build
==================

This folder is only the runtime. It does not include source, CMake, or GTA:SA.

You need
- Windows
- A legal GTA: San Andreas install (Rockstar Launcher or Steam)
- Both machines on the same network if the server is not on this PC

Start
1. On the host, run server\VantaMPServer.exe
2. On each client, run client\VantaMPLauncher.exe
   (it restarts through start_protected_game.exe for Anti-Cheat when present)
3. If the launcher cannot find the game, set game_path and game_path_manual
   in client\VantaMP_launcher.json, then start it again

Connect
- Default listen/connect address is 127.0.0.1:27015
- Remote testers: in the client UI, connect to the host LAN IP on port 27015
  (or edit client\VantaMP.json server_host)

In-game
- Esc / F1  pause menu (disconnect from there)
- F8 / ~    console

Notes
- Do not copy cache\, logs\, or crashes\ from a developer machine
- Do not ship gta_sa.exe or other game files
- Rebuild this tree after compiling with:
  powershell -File new\code\projects\vanta\scripts\package_build.ps1
'@ | Set-Content -Path (Join-Path $Dest "README.txt") -Encoding ascii

$clientSize = ((Get-ChildItem $ClientOut -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)
$serverSize = ((Get-ChildItem $ServerOut -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)
$clientCount = (Get-ChildItem $ClientOut -Recurse -File).Count
$serverCount = (Get-ChildItem $ServerOut -Recurse -File).Count

Write-Host "Packed $Dest"
Write-Host ("  client  {0} files  {1:N1} MB" -f $clientCount, $clientSize)
Write-Host ("  server  {0} files  {1:N1} MB" -f $serverCount, $serverSize)
