# Run anticheat_integritytool.exe from EOS AntiCheatTools (SDK 1.17.1.3 / tools 2.2.x)
# against a packed client tree.
#
# Default: regenerate client base.bin / runtime.conf.
# Optional: -Verify / -List
param(
    [string]$ClientDir = "",
    [switch]$Verify,
    [switch]$List
)

$ErrorActionPreference = "Stop"

$VantaRoot = Split-Path -Parent $PSScriptRoot
$FrameworkRoot = (Resolve-Path (Join-Path $VantaRoot "..\..\..")).Path
$WorkspaceRoot = (Resolve-Path (Join-Path $FrameworkRoot "..")).Path

if (-not $ClientDir) {
    $ClientDir = Join-Path $WorkspaceRoot "build\client"
}
$ClientDir = (Resolve-Path $ClientDir).Path

$EosRoot = Join-Path $VantaRoot "vendors\eos"
$EosTools = Join-Path $EosRoot "Tools"
$KeysDir = Join-Path $EosRoot "Keys"
$CfgPath = Join-Path $PSScriptRoot "anticheat_integritytool.cfg"

function Get-EosCredentialConst([string]$Name) {
    $Creds = Join-Path $VantaRoot "code\shared\eos\credentials.h"
    $pattern = "k$Name\s*=\s*`"([^`"]+)`""
    $m = Select-String -Path $Creds -Pattern $pattern | Select-Object -First 1
    if (-not $m) { throw "Could not parse k$Name from credentials.h" }
    return $m.Matches.Groups[1].Value
}

$AcZip = Get-ChildItem -Path $EosTools -Filter "EOS_AntiCheatTools-win32-x64-*.zip" -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    Select-Object -First 1
if (-not $AcZip) {
    throw "Missing EOS_AntiCheatTools-win32-x64-*.zip under $EosTools"
}

$AcExtract = Join-Path $env:TEMP ("vanta-eac-integrity-" + [guid]::NewGuid().ToString("N"))
try {
    Expand-Archive -LiteralPath $AcZip.FullName -DestinationPath $AcExtract -Force
    $Tool = Join-Path $AcExtract "devtools\anticheat_integritytool.exe"
    if (-not (Test-Path $Tool)) {
        throw "anticheat_integritytool.exe not found in $($AcZip.Name)"
    }

    $productId = Get-EosCredentialConst "ProductId"

    if ($Verify) {
        Write-Host "Verifying integrity catalog in $ClientDir"
        & $Tool -productid $productId verify $ClientDir
        exit $LASTEXITCODE
    }

    if ($List) {
        $Bin = Join-Path $ClientDir "EasyAntiCheat\Certificates\base.bin"
        Write-Host "Listing $Bin"
        & $Tool -productid $productId list $Bin
        exit $LASTEXITCODE
    }

    $InKey = Join-Path $KeysDir "base_private.key"
    $InCer = Join-Path $KeysDir "base_public.cer"
    if (-not (Test-Path $InKey)) { throw "Missing signing key: $InKey" }
    if (-not (Test-Path $InCer)) { throw "Missing certificate: $InCer" }
    if (-not (Test-Path $CfgPath)) { throw "Missing config: $CfgPath" }
    if (-not (Test-Path (Join-Path $ClientDir "start_protected_game.exe"))) {
        throw "Client tree incomplete (no start_protected_game.exe). Run scripts\build.bat pack first."
    }

    Write-Host "Generating signed integrity catalog for $ClientDir"
    Write-Host "  tool: $($AcZip.Name) / anticheat_integritytool.exe"

    & $Tool `
        -productid $productId `
        -inkey $InKey `
        -incert $InCer `
        -outname base `
        -target_game_dir $ClientDir `
        $CfgPath
    exit $LASTEXITCODE
}
finally {
    if (Test-Path $AcExtract) {
        Remove-Item $AcExtract -Recurse -Force -ErrorAction SilentlyContinue
    }
}
