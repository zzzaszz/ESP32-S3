param(
    [string]$Port = 'COM12',
    [string]$PanelUrl,
    [string]$LogPath,
    [switch]$Once,
    [switch]$Stop,
    [switch]$RequireAck,
    [int]$IntervalSeconds = 15
)
$ErrorActionPreference = 'Stop'

$pidFile = Join-Path ([IO.Path]::GetTempPath()) 'battery-bridge-esp32-s3.pid'
$stopFile = $pidFile + '.stop'
if ($Stop) {
    $stopMutex = [Threading.Mutex]::new($false, 'Local\BatteryBridge_ESP32_S3')
    $bridgeRunning = $false
    $ownsStopMutex = $false
    try {
        try {
            $ownsStopMutex = $stopMutex.WaitOne(0)
            $bridgeRunning = -not $ownsStopMutex
        } catch [Threading.AbandonedMutexException] {
            $stopMutex.ReleaseMutex()
        }
        if ($bridgeRunning) {
            Set-Content -LiteralPath $stopFile -Value 'stop' -Encoding ascii
            $deadline = [DateTime]::UtcNow.AddSeconds(15)
            while ((Test-Path -LiteralPath $pidFile) -and [DateTime]::UtcNow -lt $deadline) {
                Start-Sleep -Milliseconds 200
            }
            if (Test-Path -LiteralPath $pidFile) {
                Write-Warning 'Stop requested; BatteryBridge will exit after its current device query.'
            } else { Write-Host 'BatteryBridge stopped.' }
        } else {
            Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $stopFile -Force -ErrorAction SilentlyContinue
            Write-Host 'BatteryBridge is not running.'
        }
    } catch { Write-Warning ("Unable to stop BatteryBridge: " + $_.Exception.Message) }
    finally {
        if ($ownsStopMutex) { $stopMutex.ReleaseMutex() }
        $stopMutex.Dispose()
    }
    exit 0
}

if ($IntervalSeconds -lt 1 -or $IntervalSeconds -gt 3600) {
    throw 'IntervalSeconds must be between 1 and 3600 seconds.'
}

if ($PanelUrl) {
    try { $panelUri = [Uri]::new($PanelUrl, [UriKind]::Absolute) } catch { throw 'PanelUrl must be an absolute http or https URL.' }
    if ($panelUri.Scheme -notin @('http', 'https') -or -not $panelUri.Host) {
        throw 'PanelUrl must be an absolute http or https URL.'
    }
    if ($panelUri.UserInfo -or $panelUri.Query -or $panelUri.Fragment) {
        throw 'PanelUrl must not contain user info, query, or fragment.'
    }
    $PanelUrl = $panelUri.AbsoluteUri.TrimEnd('/')
}
if ($RequireAck -and $PanelUrl) {
    throw 'RequireAck is only supported for USB serial mode.'
}

$panelToken = [Environment]::GetEnvironmentVariable('BATTERY_PANEL_TOKEN', 'Process')
if ($panelToken -and $panelToken -notmatch '^[0-9a-fA-F]{32}$') {
    throw 'PanelToken must be a 32-character hexadecimal (128-bit) token.'
}

$mutex = [Threading.Mutex]::new($false, 'Local\BatteryBridge_ESP32_S3')
$ownsMutex = $false
try {
    $ownsMutex = $mutex.WaitOne(0)
} catch [Threading.AbandonedMutexException] {
    $ownsMutex = $true
    Write-Warning 'Recovered an abandoned BatteryBridge mutex.'
}
if (-not $ownsMutex) {
    $mutex.Dispose()
    throw 'BatteryBridge is already running. Use -Stop before starting another instance.'
}
Set-Content -LiteralPath $pidFile -Value $PID -Encoding ascii
Remove-Item -LiteralPath $stopFile -Force -ErrorAction SilentlyContinue

function Open-SerialPort {
    param([string]$Name)
    $port = $null
    try {
        $port = [IO.Ports.SerialPort]::new($Name, 115200, 'None', 8, 'One')
        $port.DtrEnable = $true
        $port.WriteTimeout = 3000
        $port.ReadTimeout = 1000
        $port.NewLine = "`n"
        $port.Open()
        Start-Sleep -Seconds 2
        Write-Host ("Serial connected: {0}" -f $Name)
        return $port
    } catch {
        if ($port) { $port.Dispose() }
        Write-Warning ("Serial unavailable ({0}): {1}" -f $Name, $_.Exception.Message)
        return $null
    }
}

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
$transcriptStarted = $false
try {
    if ($LogPath) {
        Start-Transcript -LiteralPath $LogPath -Append -NoClobber | Out-Null
        $transcriptStarted = $true
    }
    do {
        if (Test-Path -LiteralPath $stopFile) { break }
        if (-not $PanelUrl -and (-not $serial -or -not $serial.IsOpen)) {
            $serial = Open-SerialPort $Port
            if (-not $serial) {
                if ($Once) { throw "Unable to open serial port $Port" }
                for ($second = 0; $second -lt 5; $second++) {
                    if (Test-Path -LiteralPath $stopFile) { break }
                    Start-Sleep -Seconds 1
                }
                continue
            }
        }

        $level = -1; $connected = 0; $charging = -1
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
                if ($null -ne $battery.charging) {
                    $charging = [int][bool]$battery.charging
                }
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

        $sendFailed = $false
        foreach ($frame in @("BAT2,1,$level,$connected,$charging", "BAT2,0,$headsetLevel,$headsetConnected,$headsetCharging")) {
            try {
                if ($PanelUrl) {
                    $headers = @{}
                    if ($panelToken) { $headers['X-Battery-Token'] = $panelToken }
                    $request = @{
                        UseBasicParsing = $true
                        Uri = $PanelUrl.TrimEnd('/') + '/battery'
                        Method = 'Post'
                        ContentType = 'text/plain'
                        Body = $frame
                        TimeoutSec = 5
                    }
                    if ($headers.Count) { $request.Headers = $headers }
                    $null = Invoke-WebRequest @request
                } else {
                    $serial.WriteLine($frame)
                    if ($RequireAck) {
                        if ($frame -notmatch '^BAT2,(\d+),(-?\d+),') {
                            throw 'Unable to derive expected BAT2 ACK.'
                        }
                        $expectedAck = "ACK BAT2 $($Matches[1]) $($Matches[2])"
                        $reply = ''
                        $deadline = [DateTime]::UtcNow.AddSeconds(3)
                        while ([DateTime]::UtcNow -lt $deadline -and $reply -notmatch [Regex]::Escape($expectedAck)) {
                            $reply += $serial.ReadExisting()
                            Start-Sleep -Milliseconds 100
                        }
                        if ($reply -notmatch [Regex]::Escape($expectedAck)) {
                            throw "Timed out waiting for $expectedAck."
                        }
                    }
                }
            } catch {
                $sendFailed = $true
                Write-Warning ("Battery frame send failed: " + $_.Exception.Message)
            }
        }
        if ($sendFailed -and $serial) {
            if ($serial.IsOpen) { $serial.Close() }
            $serial.Dispose()
            $serial = $null
        }
        if ($sendFailed -and $Once) {
            throw 'Unable to send one or more battery frames.'
        }
        if ($serial -and -not $RequireAck) {
            try {
                Start-Sleep -Milliseconds 300
                $reply = $serial.ReadExisting()
                if ($reply) { Write-Host $reply.Trim() }
            } catch {
                Write-Warning ("Serial read failed: " + $_.Exception.Message)
                if ($serial.IsOpen) { $serial.Close() }
                $serial.Dispose()
                $serial = $null
            }
        }
        Write-Host ("{0:HH:mm:ss} PRO Wireless: {1}% connected={2} charging={3}" -f (Get-Date), $level, $connected, $charging)
        Write-Host ("{0:HH:mm:ss} HyperX Cloud III S: {1}% connected={2} charging={3}" -f (Get-Date), $headsetLevel, $headsetConnected, $headsetCharging)
        if (-not $Once) {
            for ($second = 0; $second -lt $IntervalSeconds; $second++) {
                if (Test-Path -LiteralPath $stopFile) { break }
                Start-Sleep -Seconds 1
            }
        }
    } while (-not $Once -and -not (Test-Path -LiteralPath $stopFile))
} finally {
    if ($serial) { if ($serial.IsOpen) { $serial.Close() }; $serial.Dispose() }
    if (Test-Path -LiteralPath $pidFile) {
        try {
            if ([int](Get-Content -LiteralPath $pidFile -Raw).Trim() -eq $PID) {
                Remove-Item -LiteralPath $pidFile -Force
            }
        } catch { }
    }
    Remove-Item -LiteralPath $stopFile -Force -ErrorAction SilentlyContinue
    if ($mutex) {
        if ($ownsMutex) { $mutex.ReleaseMutex() }
        $mutex.Dispose()
    }
    if ($transcriptStarted) { Stop-Transcript | Out-Null }
}
