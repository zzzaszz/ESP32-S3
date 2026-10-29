param(
    [string]$Port = 'COM12',
    [string]$PanelUrl,
    [string]$LogPath,
    [switch]$Once,
    [switch]$Stop,
    [switch]$RequireAck,
    [switch]$OfflineTestMode,
    [switch]$TraceOrientation,
    [int]$IntervalSeconds = 2
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'PanelProtocol.ps1')
$script:panelLineBuffer = ''
$script:refreshRequested = $false
$lastRefreshSequence = $null

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
if ($OfflineTestMode -and (-not $Once -or -not $PanelUrl -or -not $panelUri.IsLoopback)) {
    throw 'OfflineTestMode requires -Once and a loopback -PanelUrl.'
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
        $port.Encoding = [Text.Encoding]::UTF8
        $port.DtrEnable = $true
        $port.WriteTimeout = 3000
        $port.ReadTimeout = 1000
        $port.NewLine = "`n"
        $port.Open()
        $script:panelLineBuffer = ''
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

function Get-ComputerPerformance {
    $cpu = -1; $cpuTemperature = -1; $memory = -1; $gpu = -1; $gpuTemperature = -1
    $uploadKbps = 0; $downloadKbps = 0; $diskFreeGb = 0; $uptimeSeconds = 0
    try {
        $sample = Get-Counter '\Processor(_Total)\% Processor Time' -ErrorAction Stop
        $cpu = [Math]::Max(0, [Math]::Min(100, [int][Math]::Round($sample.CounterSamples[0].CookedValue)))
    } catch { }
    try {
        $zones = @(Get-CimInstance -Namespace 'root/wmi' -ClassName MSAcpi_ThermalZoneTemperature -ErrorAction Stop |
            Where-Object { $_.CurrentTemperature -gt 0 -and $_.InstanceName -match 'CPU|PROC|TCPU|PACKAGE|CORE' } |
            ForEach-Object { [int][Math]::Round(($_.CurrentTemperature / 10) - 273.15) } |
            Where-Object { $_ -ge 0 -and $_ -le 150 })
        if ($zones.Count) { $cpuTemperature = $zones[0] }
    } catch { }
    try {
        $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
        if ([double]$os.TotalVisibleMemorySize -gt 0) {
            $memory = [int][Math]::Round((1 - ($os.FreePhysicalMemory / $os.TotalVisibleMemorySize)) * 100)
            $memory = [Math]::Max(0, [Math]::Min(100, $memory))
        }
        $boot = [DateTime]$os.LastBootUpTime
        $uptimeSeconds = [uint32][Math]::Min([uint64]([uint32]::MaxValue), [Math]::Max(0, ((Get-Date) - $boot).TotalSeconds))
    } catch { }
    try {
        $gpuSamples = @(Get-Counter '\GPU Engine(*)\Utilization Percentage' -ErrorAction Stop).CounterSamples |
            Where-Object InstanceName -Match '_engtype_3D' |
            Measure-Object -Property CookedValue -Sum
        if ($gpuSamples.Count -and $gpuSamples.Sum -ge 0) {
            $gpu = [int][Math]::Max(0, [Math]::Min(100, [Math]::Round($gpuSamples.Sum)))
        }
    } catch { }
    try {
        $temperatures = @(Get-CimInstance Win32_VideoController -ErrorAction Stop |
            Where-Object { $_.CurrentTemperature -gt 2000 } |
            ForEach-Object { [int][Math]::Round(($_.CurrentTemperature / 10) - 273.15) })
        if ($temperatures.Count) { $gpuTemperature = $temperatures[0] }
    } catch { }
    if ($gpuTemperature -lt 0) {
        try {
            if (-not ('AmdGpuTemperature' -as [type])) {
                Add-Type -Path (Join-Path $PSScriptRoot 'AmdGpuTemperature.cs')
            }
            $gpuTemperature = [AmdGpuTemperature]::ReadCelsius()
            Write-Host ("GPU temperature source: " + [AmdGpuTemperature]::Diagnostic)
        } catch { $gpuTemperature = -1 }
    }
    try {
        $network = @(Get-Counter '\Network Interface(*)\Bytes Received/sec','\Network Interface(*)\Bytes Sent/sec' -ErrorAction Stop).CounterSamples
        $downloadKbps = [uint32][Math]::Max(0, [Math]::Round((($network | Where-Object Path -Match 'Bytes Received/sec' | Measure-Object CookedValue -Sum).Sum) / 1024))
        $uploadKbps = [uint32][Math]::Max(0, [Math]::Round((($network | Where-Object Path -Match 'Bytes Sent/sec' | Measure-Object CookedValue -Sum).Sum) / 1024))
    } catch { }
    try {
        $disk = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='C:'" -ErrorAction Stop
        if ($disk.FreeSpace -ne $null) { $diskFreeGb = [uint32][Math]::Max(0, [Math]::Round($disk.FreeSpace / 1GB)) }
    } catch { }
    [PSCustomObject]@{
        Cpu = $cpu; Memory = $memory; Gpu = $gpu; GpuTemperature = $gpuTemperature
        UploadKbps = $uploadKbps; DownloadKbps = $downloadKbps; DiskFreeGb = $diskFreeGb
        UptimeSeconds = $uptimeSeconds
    }
}

function Send-PanelFrame([string]$Frame) {
    try {
        if ($PanelUrl) {
            $headers = @{}
            if ($panelToken) { $headers['X-Battery-Token'] = $panelToken }
            $request = @{
                UseBasicParsing = $true; Uri = $PanelUrl.TrimEnd('/') + '/battery'
                Method = 'Post'; ContentType = 'text/plain'; Body = $Frame; TimeoutSec = 5
            }
            if ($headers.Count) { $request.Headers = $headers }
            $null = Invoke-WebRequest @request
        } else {
            $serial.WriteLine($Frame)
            if ($RequireAck) {
                $expectedAck = Get-PanelAck $Frame
                $reply = ''
                $deadline = [DateTime]::UtcNow.AddSeconds(3)
                while ([DateTime]::UtcNow -lt $deadline -and -not (Test-PanelAck $reply $expectedAck)) {
                    $lines = @(Read-PanelOutput $serial)
                    $lines | Where-Object { $_ -match '^ORIENTATION ' } | ForEach-Object { Write-Host $_ }
                    if ($lines.Count) { $reply += ($lines -join "`n") + "`n" }
                    if (-not (Test-PanelAck $reply $expectedAck)) { Start-Sleep -Milliseconds 25 }
                }
                if (-not (Test-PanelAck $reply $expectedAck)) { throw "Timed out waiting for $expectedAck." }
                Write-Host $expectedAck
            }
        }
        Write-Host ("{0:HH:mm:ss.fff} SENT {1}" -f (Get-Date), $Frame)
        return $true
    } catch {
        Write-Warning ("Battery frame send failed: " + $_.Exception.Message)
        return $false
    }
}

$serial = $null
$performance = $null
$lastPerformancePoll = [DateTime]::MinValue
$hyperXPaths = @()
$lastHidDiscovery = [DateTime]::MinValue
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

        $cycleStarted = [DateTime]::UtcNow
        $sendFailed = $false
        if ($TraceOrientation -and $serial -and $serial.IsOpen) {
            try { $serial.WriteLine('ORIENTATION?') } catch { Write-Warning $_.Exception.Message }
        }
        if ($OfflineTestMode) {
            $level = -1; $connected = 0; $charging = -1
            $headsetLevel = -1; $headsetConnected = 0; $headsetCharging = -1
            $performance = [PSCustomObject]@{
                Cpu = 42; CpuTemperature = 55; Memory = 68; Gpu = 77; GpuTemperature = 65
                UploadKbps = 12; DownloadKbps = 345; DiskFreeGb = 128; UptimeSeconds = 86400
            }
        } else {
            $level = -1; $connected = 0; $charging = -1
            $socket = [Net.WebSockets.ClientWebSocket]::new()
            $socket.Options.AddSubProtocol('json')
            $cts = [Threading.CancellationTokenSource]::new(6000)
            try {
                $null = $socket.ConnectAsync([Uri]'ws://127.0.0.1:9010', $cts.Token).GetAwaiter().GetResult()
                $list = Invoke-GHubGet $socket '/devices/list'
                $mouse = $list.deviceInfos | Where-Object deviceModel -eq 'pro_wireless_mouse' | Select-Object -First 1
                Write-Host ("G HUB PRO Wireless state=" + $(if ($mouse) { $mouse.state } else { 'NOT_FOUND' }))
                if ($mouse -and $mouse.state -eq 'ACTIVE') {
                    $connected = 1
                    $battery = Invoke-GHubGet $socket ("/battery/{0}/state" -f $mouse.id)
                    $reading = Convert-GHubBattery $battery $mouse.id
                    $level = $reading.Level
                    $charging = $reading.Charging
                }
            } catch {
                $level = -1; $connected = 0; $charging = -1
                Write-Warning ("G HUB unavailable: " + $_.Exception.Message)
            }
            finally { $socket.Dispose(); $cts.Dispose() }
            # A slow headset query or PC counter must not hold up an online mouse.
            if (-not (Send-PanelFrame "BAT2,1,$level,$connected,$charging")) { $sendFailed = $true }

            $headsetLevel = -1; $headsetConnected = 0; $headsetCharging = -1
            try {
                if (([DateTime]::UtcNow - $lastHidDiscovery).TotalSeconds -ge 30) {
                    $hyperXPaths = @(Get-PnpDevice -PresentOnly -Class HIDClass |
                        Where-Object InstanceId -like 'HID\VID_03F0&PID_02CC*' |
                        ForEach-Object { '\\?\' + $_.InstanceId.Replace('\','#') + '#{4d1e55b2-f16f-11cf-88cb-001111000030}' })
                    $lastHidDiscovery = [DateTime]::UtcNow
                }
                $headset = & (Join-Path $PSScriptRoot 'Read-HyperX.ps1') -Paths $hyperXPaths
                Write-Host ("HyperX raw query: " + $headset.Diagnostic)
                if ($headset.Level -ge 0 -and $headset.Level -le 100) {
                    $headsetLevel = $headset.Level
                    $headsetConnected = 1
                }
                if ($headset.Charging -ge 0 -and $headset.Charging -le 2) {
                    $headsetCharging = [int]($headset.Charging -gt 0)
                }
            } catch { Write-Warning ("HyperX unavailable: " + $_.Exception.Message) }
            if (-not (Send-PanelFrame "BAT2,0,$headsetLevel,$headsetConnected,$headsetCharging")) { $sendFailed = $true }

            if (-not $performance -or ([DateTime]::UtcNow - $lastPerformancePoll).TotalSeconds -ge 15) {
                $performance = Get-ComputerPerformance
                $lastPerformancePoll = [DateTime]::UtcNow
            }
        }
        $performanceFrame = "PERF,{0},{1},{2},{3},{4},{5},{6},{7},{8}" -f `
            $performance.Cpu, $performance.Memory, $performance.Gpu, $performance.GpuTemperature,
            $performance.UploadKbps, $performance.DownloadKbps, $performance.DiskFreeGb,
            $performance.UptimeSeconds, $performance.CpuTemperature
        $frames = @($performanceFrame)
        if ($OfflineTestMode) { $frames = @("BAT2,1,$level,$connected,$charging", "BAT2,0,$headsetLevel,$headsetConnected,$headsetCharging", $performanceFrame) }
        foreach ($frame in $frames) {
            if (-not (Send-PanelFrame $frame)) { $sendFailed = $true }
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
                $reply = @(Read-PanelOutput $serial)
                if ($reply.Count) { Write-Host ($reply -join "`n") }
            } catch {
                Write-Warning ("Serial read failed: " + $_.Exception.Message)
                if ($serial.IsOpen) { $serial.Close() }
                $serial.Dispose()
                $serial = $null
            }
        }
        Write-Host ("{0:HH:mm:ss} PRO Wireless: {1}% connected={2} charging={3}" -f (Get-Date), $level, $connected, $charging)
        Write-Host ("{0:HH:mm:ss} HyperX Cloud III S: {1}% connected={2} charging={3}" -f (Get-Date), $headsetLevel, $headsetConnected, $headsetCharging)
        Write-Host ("{0:HH:mm:ss} PC: cpu={1}% cpuTemp={2}C memory={3}% gpu={4}% gpuTemp={5}C netUp={6}KB/s netDown={7}KB/s" -f
            (Get-Date), $performance.Cpu, $performance.CpuTemperature, $performance.Memory, $performance.Gpu, $performance.GpuTemperature,
            $performance.UploadKbps, $performance.DownloadKbps)
        if (-not $Once) {
            # Interval is measured from query start; expensive counters no longer
            # add a second full interval after each collection.
            $nextPoll = $cycleStarted.AddSeconds($IntervalSeconds)
            $nextHealth = [DateTime]::UtcNow
            while ([DateTime]::UtcNow -lt $nextPoll) {
                if (Test-Path -LiteralPath $stopFile) { break }
                if ($serial -and $serial.IsOpen) {
                    try {
                        $output = @(Read-PanelOutput $serial)
                        if ($output.Count) { Write-Host ($output -join "`n") }
                    } catch { break }
                } elseif ($PanelUrl -and [DateTime]::UtcNow -ge $nextHealth) {
                    $nextHealth = [DateTime]::UtcNow.AddSeconds(1)
                    try {
                        $health = Invoke-WebRequest -UseBasicParsing -Uri ($PanelUrl + '/health') -TimeoutSec 2
                        if ($health.Content -match '(?m)^refresh=(\d+)\s*$') {
                            $sequence = $Matches[1]
                            if ($null -ne $lastRefreshSequence -and $lastRefreshSequence -ne $sequence) { $script:refreshRequested = $true }
                            $lastRefreshSequence = $sequence
                        }
                    } catch { }
                }
                if ($script:refreshRequested) {
                    $script:refreshRequested = $false
                    Write-Host 'Panel requested immediate battery read.'
                    break
                }
                Start-Sleep -Milliseconds 100
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
