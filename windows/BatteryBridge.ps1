param(
    [string]$Port = 'COM12',
    [string]$PanelUrl,
    [switch]$Once,
    [int]$IntervalSeconds = 15
)
$ErrorActionPreference = 'Stop'

function Invoke-GHubGet($Socket, [string]$Path) {
    $cts = [Threading.CancellationTokenSource]::new(6000)
    try {
        $id = [Guid]::NewGuid().ToString('N')
        $request = @{path=$Path; verb='GET'; msgId=$id} | ConvertTo-Json -Compress
        $bytes = [Text.Encoding]::UTF8.GetBytes($request)
        $null = $Socket.SendAsync([ArraySegment[byte]]::new($bytes), [Net.WebSockets.WebSocketMessageType]::Text, $true, $cts.Token).GetAwaiter().GetResult()
        do {
            $stream = [IO.MemoryStream]::new()
            try {
                do {
                    $buffer = New-Object byte[] 8192
                    $result = $Socket.ReceiveAsync([ArraySegment[byte]]::new($buffer), $cts.Token).GetAwaiter().GetResult()
                    if ($result.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) { throw 'G HUB closed connection' }
                    $stream.Write($buffer, 0, $result.Count)
                    if ($stream.Length -gt 1048576) { throw 'G HUB response too large' }
                } until ($result.EndOfMessage)
                $message = [Text.Encoding]::UTF8.GetString($stream.ToArray()) | ConvertFrom-Json
            } finally { $stream.Dispose() }
        } until ($message.msgId -eq $id)
        if ($message.result.code -ne 'SUCCESS') { throw "G HUB GET failed: $Path" }
        return $message.payload
    } finally { $cts.Dispose() }
}

$serial = $null
try {
    if (-not $PanelUrl) {
        $serial = [IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
        $serial.DtrEnable = $true
        $serial.WriteTimeout = 3000
        $serial.NewLine = "`n"
        $serial.Open()
        Start-Sleep -Seconds 2
    }
    do {
        $level = -1; $connected = 0; $charging = 0
        $socket = [Net.WebSockets.ClientWebSocket]::new()
        $socket.Options.AddSubProtocol('json')
        $cts = [Threading.CancellationTokenSource]::new(6000)
        try {
            $null = $socket.ConnectAsync([Uri]'ws://127.0.0.1:9010', $cts.Token).GetAwaiter().GetResult()
            $list = Invoke-GHubGet $socket '/devices/list'
            $mouse = $list.deviceInfos | Where-Object deviceModel -eq 'pro_wireless_mouse' | Select-Object -First 1
            if ($mouse -and $mouse.state -eq 'ACTIVE') {
                $connected = 1
                $battery = Invoke-GHubGet $socket ("/battery/{0}/state" -f $mouse.id)
                if ($null -ne $battery.percentage -and -not $battery.isBatteryRemoved) {
                    $value = [double]$battery.percentage
                    if ($value -ge 0 -and $value -le 100) { $level = [int][Math]::Round($value) }
                }
                $charging = [int][bool]$battery.charging
            }
        } catch { Write-Warning ("G HUB unavailable: " + $_.Exception.Message) }
        finally { $socket.Dispose(); $cts.Dispose() }

        $headsetLevel = -1; $headsetConnected = 0; $headsetCharging = -1
        try {
            $headset = & (Join-Path $PSScriptRoot 'Read-HyperX.ps1')
            if ($headset.Level -ge 0 -and $headset.Level -le 100) {
                $headsetLevel = $headset.Level
                $headsetConnected = 1
            }
            if ($headset.Charging -ge 0 -and $headset.Charging -le 2) {
                $headsetCharging = [int]($headset.Charging -gt 0)
            }
        } catch { Write-Warning ("HyperX unavailable: " + $_.Exception.Message) }

        foreach ($frame in @("BAT2,1,$level,$connected,$charging", "BAT2,0,$headsetLevel,$headsetConnected,$headsetCharging")) {
            if ($PanelUrl) {
                $null = Invoke-WebRequest -UseBasicParsing -Uri ($PanelUrl.TrimEnd('/') + '/battery') -Method Post -ContentType 'text/plain' -Body $frame -TimeoutSec 5
            } else { $serial.WriteLine($frame) }
        }
        if ($serial) {
            Start-Sleep -Milliseconds 300
            $reply = $serial.ReadExisting()
            if ($reply) { Write-Host $reply.Trim() }
        }
        Write-Host ("{0:HH:mm:ss} PRO Wireless: {1}% connected={2} charging={3}" -f (Get-Date), $level, $connected, $charging)
        Write-Host ("{0:HH:mm:ss} HyperX Cloud III S: {1}% connected={2} charging={3}" -f (Get-Date), $headsetLevel, $headsetConnected, $headsetCharging)
        if (-not $Once) { Start-Sleep -Seconds $IntervalSeconds }
    } while (-not $Once)
} finally {
    if ($serial) { if ($serial.IsOpen) { $serial.Close() }; $serial.Dispose() }
}
