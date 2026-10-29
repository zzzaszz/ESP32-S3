param([string]$Port = 'COM12')
$ErrorActionPreference = 'Stop'
# Explicit hardware test: briefly displays synthetic values, then clears them.
# Stop BatteryBridge and Arduino Serial Monitor before running this script.
$serial = [IO.Ports.SerialPort]::new($Port, 115200)
$serial.Encoding = [Text.Encoding]::UTF8
$serial.DtrEnable = $true
$serial.NewLine = "`n"
$serial.WriteTimeout = 3000
function Send-Command([string]$Command) {
    $serial.WriteLine($Command)
    Start-Sleep -Milliseconds 180
    $text = $serial.ReadExisting()
    Write-Host ($Command + ' => ' + $text.Trim())
    return $text
}
function Assert-Match([string]$Text, [string]$Pattern) {
    if ($Text -notmatch $Pattern) { throw "Expected $Pattern, received $Text" }
}
try {
    $serial.Open()
    Start-Sleep -Milliseconds 1800
    Write-Host $serial.ReadExisting()
    Assert-Match (Send-Command 'VERSION?') 'VERSION BatteryPanel/2'
    Assert-Match (Send-Command 'PAGE 1') 'page=0'
    Assert-Match (Send-Command 'UI?') 'screen=320x240.*font=12.*lock=removed.*boundsErrors=0'
    Assert-Match (Send-Command 'UI?') 'fontBitmap=ready'
    Assert-Match (Send-Command 'PERF,42,68,77,65,12,345,128,86400') 'ACK PERF 42 68 77 65'
    Assert-Match (Send-Command 'UI?') 'metric=1 label=CPU TEMP value=--'
    Assert-Match (Send-Command 'PERF,42,68,77,65,12,345,128,86400,55') 'ACK PERF 42 68 77 65'
    $metrics = Send-Command 'UI?'
    Assert-Match $metrics 'perfSeen=1.*monitorVisible=0'
    Assert-Match $metrics 'metric=0 label=CPU value=42%'
    Assert-Match $metrics 'metric=1 label=CPU TEMP value=55 C'
    Assert-Match $metrics 'metric=2 label=GPU value=77%'
    Assert-Match $metrics 'metric=3 label=GPU TEMP value=65 C'
    Assert-Match $metrics 'metric=4 label=MEM value=68%'
    Assert-Match $metrics 'metric=5 label=UP value=12 KB/s'
    Assert-Match $metrics 'metric=6 label=DOWN value=345 KB/s'
    Assert-Match (Send-Command 'SWIPE RIGHT') 'page=1'
    Assert-Match (Send-Command 'UI?') '(?s)monitorVisible=1.*metric=0 label=CPU value=42%'
    Assert-Match (Send-Command 'SWIPE LEFT') 'page=0'
    foreach ($level in @(0, 7, 71, 100, -1)) {
        Assert-Match (Send-Command "BAT2,0,$level,1,-1") "(?m)^ACK BAT2 0 $level\r?$"
        $shown = if ($level -lt 0) { '--' } else { "$level%" }
        Assert-Match (Send-Command 'UI?') ('device=0 level=' + [Regex]::Escape($shown) + ' connected=1 charging=-1 fresh=1')
    }
    Assert-Match (Send-Command 'BAT2,1,62,1,1') 'ACK BAT2 1 62'
    Assert-Match (Send-Command 'BAT2,0,88,0,0') 'ACK BAT2 0 88'
    $ui = Send-Command 'UI?'
    Assert-Match $ui 'device=0 level=-- connected=0'
    Assert-Match $ui 'device=1 level=62% connected=1 charging=1'
    foreach ($frame in @('BAT2,1,101,1,0', 'BAT2,9,50,1,0', 'BAT2,0,50,1,2', 'BAT2,1,50,1,0junk')) {
        Assert-Match (Send-Command $frame) 'ERR FRAME'
    }
    Assert-Match (Send-Command 'UI?') 'device=1 level=62% connected=1 charging=1'
    Assert-Match (Send-Command 'BAT1,7,1,0') 'ACK BAT1 7'
    Assert-Match (Send-Command 'UI?') 'device=1 level=7% connected=1 charging=0'
    Assert-Match (Send-Command 'REFRESH') 'REQUEST BATTERY \d+'
    Assert-Match (Send-Command 'UI?') 'refreshing=1'
    $null = Send-Command 'BAT2,0,-1,0,-1'
    Assert-Match (Send-Command 'UI?') 'refreshing=1'
    $null = Send-Command 'BAT2,1,0,1,0'
    Assert-Match (Send-Command 'UI?') 'refreshing=0'
    Start-Sleep -Milliseconds 1600
    Assert-Match (Send-Command 'REFRESH') 'REQUEST BATTERY \d+'
    if ((Send-Command 'REFRESH') -match 'REQUEST BATTERY') { throw 'Refresh debounce failed.' }
    Write-Host 'Waiting 21 seconds for missing-bridge refresh timeout.'
    Start-Sleep -Seconds 21
    Assert-Match (Send-Command 'UI?') 'refreshing=0'
    $null = Send-Command 'BAT2,0,71,1,0'
    $null = Send-Command 'BAT2,1,7,1,0'
    Write-Host 'Waiting 47 seconds for independent device expiry.'
    Start-Sleep -Seconds 25
    $null = Send-Command 'BAT2,1,7,1,0'
    Start-Sleep -Seconds 22
    $health = Send-Command 'HEALTH?'
    Assert-Match $health 'device0=expired'
    Assert-Match $health 'device1=fresh'
    $ui = Send-Command 'UI?'
    Assert-Match $ui 'device=0 level=-- connected=0 charging=-1 fresh=0'
    Assert-Match $ui 'device=1 level=7% connected=1 charging=0 fresh=1'
    Assert-Match $ui 'boundsErrors=0'
    Write-Host 'Hardware protocol, rendered values, refresh lifecycle and independent expiry passed.'
    Write-Host 'Physical pixels, touch gesture direction and real battery accuracy require separate verification.'
} finally {
    if ($serial.IsOpen) {
        # Never leave synthetic percentages on the unattended display.
        $serial.WriteLine('BAT2,0,-1,0,-1')
        $serial.WriteLine('BAT2,1,-1,0,-1')
        Start-Sleep -Milliseconds 200
    }
    $serial.Dispose()
}
