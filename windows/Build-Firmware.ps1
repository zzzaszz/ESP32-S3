param(
    [string]$CliPath,
    [string]$Fqbn = 'esp32:esp32:esp32s3:CDCOnBoot=cdc',
    [switch]$WarningsAll,
    [switch]$KeepStaging
)
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$cliConfigArgs = @()
if (-not $CliPath) {
    $command = Get-Command arduino-cli -ErrorAction SilentlyContinue
    $candidates = @(
        $command.Source,
        'E:\ArduinoIDE\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe',
        (Join-Path $env:TEMP 'esp32-s3-arduino-validation\tools\arduino-cli.exe')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
    $CliPath = $candidates | Select-Object -First 1
}
if (-not $CliPath -or -not (Test-Path -LiteralPath $CliPath)) {
    throw 'Install Arduino CLI, pass -CliPath, or use the validation CLI path from README.'
}
if ($CliPath -like '*esp32-s3-arduino-validation*') {
    $cliConfigArgs = @('--config-file', (Join-Path $env:TEMP 'esp32-s3-arduino-validation\config\arduino-cli.yaml'))
}

$staging = Join-Path $env:TEMP ('battery-panel-build-' + [Guid]::NewGuid().ToString('N'))
$project = Join-Path $staging 'ESP32-S3-Touch-LCD-2.8-Battery-Display'
$removeStaging = -not $KeepStaging
try {
    $null = New-Item -ItemType Directory -Path $project
    Get-ChildItem -LiteralPath $repo -File |
        Where-Object Extension -in '.ino', '.c', '.cpp', '.h' |
        Copy-Item -Destination $project

    $warningArgs = @()
    if ($WarningsAll) { $warningArgs = @('--warnings', 'all') }
    & $CliPath @cliConfigArgs compile @warningArgs --fqbn $Fqbn $project
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) { throw ("Arduino CLI compile failed with exit code {0}." -f $exitCode) }
    Write-Host ("Firmware build passed. Staging: {0}" -f $project)
} finally {
    if ($removeStaging -and (Test-Path -LiteralPath $staging)) {
        Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
    }
}
