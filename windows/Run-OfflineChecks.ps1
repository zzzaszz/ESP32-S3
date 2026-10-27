param(
    [switch]$BuildFirmware
)
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$scriptPaths = @(
    'windows/BatteryBridge.ps1',
    'windows/Set-PanelToken.ps1',
    'windows/Read-HyperX.ps1',
    'windows/Test-BatteryBridge.ps1',
    'windows/Test-FirmwareAssets.ps1',
    'windows/Build-Firmware.ps1',
    'windows/Run-OfflineChecks.ps1'
)

$errors = @()
foreach ($relativePath in $scriptPaths) {
    $path = Join-Path $repo $relativePath
    [System.Management.Automation.Language.Parser]::ParseFile(
        $path, [ref]$null, [ref]$errors
    ) | Out-Null
}
if ($errors.Count) {
    $errors | ForEach-Object { Write-Error $_.ToString() }
    throw 'PowerShell script parsing failed.'
}
Write-Host ("PowerShell parse passed ({0} scripts)." -f $scriptPaths.Count)

$assetTest = Join-Path $PSScriptRoot 'Test-FirmwareAssets.ps1'
& $assetTest

$testScript = Join-Path $PSScriptRoot 'Test-BatteryBridge.ps1'
& $testScript -ValidationOnly
& $testScript -WithToken
& $testScript -FirstResponseError -WithToken
& $testScript

$pidFile = Join-Path ([IO.Path]::GetTempPath()) 'battery-bridge-esp32-s3.pid'
$stopFile = $pidFile + '.stop'
if ((Test-Path -LiteralPath $pidFile) -or (Test-Path -LiteralPath $stopFile)) {
    throw 'BatteryBridge left a PID or stop file after offline checks.'
}
Write-Host 'BatteryBridge cleanup check passed.'

if ($BuildFirmware) {
    & (Join-Path $PSScriptRoot 'Build-Firmware.ps1') -WarningsAll
}

Write-Host 'Offline validation passed. Hardware upload, display, touch, Wi-Fi, and long-run checks remain separate.'
