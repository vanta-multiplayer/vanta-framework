# Dump Framework / Vanta scripting API catalogs via FRAMEWORK_SCRIPTING_API_METADATA.
#
#   scripts\dump_scripting_catalog.ps1 -Side server
#   scripts\dump_scripting_catalog.ps1 -Side client
#
# Server: starts VantaMPServer briefly until the JSON appears, then stops it.
# Client: prints launch instructions — client scripting inits inside the game.

param(
    [ValidateSet("server", "client")]
    [string]$Side = "server",
    [int]$TimeoutSec = 45
)

$ErrorActionPreference = "Stop"
$Vanta = Split-Path -Parent $PSScriptRoot
$CatalogDir = Join-Path $Vanta "types\catalog"
New-Item -ItemType Directory -Force -Path $CatalogDir | Out-Null
$OutFile = Join-Path $CatalogDir "$Side.json"

$FrameworkRoot = $env:VANTA_FRAMEWORK
if (-not $FrameworkRoot) {
    $marker = Join-Path $Vanta ".framework-root"
    if (Test-Path $marker) {
        $FrameworkRoot = (Get-Content $marker -TotalCount 1).Trim()
    }
}
if (-not $FrameworkRoot -or -not (Test-Path (Join-Path $FrameworkRoot "builds\build.bat"))) {
    $FrameworkRoot = (Resolve-Path (Join-Path $Vanta "..\..\..")).Path
}
if (-not (Test-Path (Join-Path $FrameworkRoot "builds\build.bat"))) {
    Write-Error "Framework root not found (looked for builds\build.bat). Set VANTA_FRAMEWORK."
}

$env:FRAMEWORK_SCRIPTING_API_METADATA = $OutFile
if (Test-Path $OutFile) {
    Remove-Item -Force $OutFile
}

if ($Side -eq "server") {
    $ServerExe = Join-Path $FrameworkRoot "builds\build-64\bin\VantaMPServer.exe"
    if (-not (Test-Path $ServerExe)) {
        Write-Error "Server not found: $ServerExe - build with scripts\build.bat server first."
    }

    Write-Host "Dumping server catalog to $OutFile"
    Write-Host "Starting $ServerExe ..."
    $proc = Start-Process -FilePath $ServerExe -WorkingDirectory (Split-Path $ServerExe) -PassThru -WindowStyle Hidden

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $OutFile) {
            $len = (Get-Item $OutFile).Length
            if ($len -gt 64) {
                Start-Sleep -Milliseconds 400
                break
            }
        }
        if ($proc.HasExited) {
            break
        }
        Start-Sleep -Milliseconds 250
    }

    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        try { Wait-Process -Id $proc.Id -Timeout 5 } catch {}
    }

    if (-not (Test-Path $OutFile)) {
        Write-Error "Catalog dump failed: $OutFile missing"
    }
    $bytes = (Get-Item $OutFile).Length
    if ($bytes -lt 64) {
        Write-Error "Catalog dump empty: $OutFile"
    }

    Write-Host "Wrote $OutFile ($bytes bytes)"
    exit 0
}

Write-Host "Client catalog path: $OutFile"
Write-Host "Set the env var, then launch the client so scripting init writes the dump:"
Write-Host ('  $env:FRAMEWORK_SCRIPTING_API_METADATA = "{0}"' -f $OutFile)
$Launcher = Join-Path $FrameworkRoot "builds\build\bin\VantaMPLauncher.exe"
if (Test-Path $Launcher) {
    Write-Host ('  & "{0}"' -f $Launcher)
} else {
    Write-Host "  (launcher not found at $Launcher - build client first)"
}
Write-Host "When the file appears, stop the game and run:"
Write-Host "  python scripts\generate_scripting_types.py"
