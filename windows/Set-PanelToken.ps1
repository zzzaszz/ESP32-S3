param([string]$Port = 'COM12')
$ErrorActionPreference = 'Stop'

$token = [Environment]::GetEnvironmentVariable('BATTERY_PANEL_TOKEN', 'Process')
if ($token -notmatch '^[0-9a-fA-F]{32}$') {
    throw 'Set BATTERY_PANEL_TOKEN to a 32-character hexadecimal (128-bit) token in this PowerShell process.'
}

$serial = [IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
$serial.DtrEnable = $true
$serial.NewLine = "`n"
$serial.ReadTimeout = 1000
try {
    $serial.Open()
    Start-Sleep -Seconds 2
    $null = $serial.ReadExisting()
    $serial.WriteLine("TOKEN`t$token")

    $reply = ''
    $deadline = [DateTime]::UtcNow.AddSeconds(4)
    while ([DateTime]::UtcNow -lt $deadline -and $reply -notmatch 'TOKEN SAVED') {
        $reply += $serial.ReadExisting()
        Start-Sleep -Milliseconds 100
    }
    if ($reply -notmatch 'TOKEN SAVED') { throw 'Panel did not confirm token storage.' }
    Write-Host 'Panel token saved. The token value was not displayed.'
} finally {
    if ($serial) {
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
    }
}
