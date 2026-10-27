param(
    [switch]$WithToken,
    [switch]$FirstResponseError,
    [switch]$ValidationOnly
)
$ErrorActionPreference = 'Stop'

$bridge = Join-Path $PSScriptRoot 'BatteryBridge.ps1'
if ($ValidationOnly) {
    $cases = @(
        [PSCustomObject]@{
            Parameters = @{ IntervalSeconds = 0; Once = $true }
            Description = '-IntervalSeconds 0 -Once'
            Pattern = 'IntervalSeconds must be between'
        },
        [PSCustomObject]@{
            Parameters = @{ IntervalSeconds = 3601; Once = $true }
            Description = '-IntervalSeconds 3601 -Once'
            Pattern = 'IntervalSeconds must be between'
        },
        [PSCustomObject]@{
            Parameters = @{ PanelUrl = 'ftp://127.0.0.1/panel'; Once = $true }
            Description = '-PanelUrl ftp://127.0.0.1/panel -Once'
            Pattern = 'PanelUrl must be an absolute'
        },
        [PSCustomObject]@{
            Parameters = @{ PanelUrl = 'http://127.0.0.1/panel?token=secret'; Once = $true }
            Description = '-PanelUrl http://127.0.0.1/panel?token=secret -Once'
            Pattern = 'must not contain user info'
        },
        [PSCustomObject]@{
            Parameters = @{ PanelUrl = 'http://127.0.0.1/panel#fragment'; Once = $true }
            Description = '-PanelUrl http://127.0.0.1/panel#fragment -Once'
            Pattern = 'must not contain user info'
        },
        [PSCustomObject]@{
            Parameters = @{ PanelUrl = 'http://user@127.0.0.1/panel'; Once = $true }
            Description = '-PanelUrl http://user@127.0.0.1/panel -Once'
            Pattern = 'must not contain user info'
        },
        [PSCustomObject]@{
            Parameters = @{ PanelUrl = 'http://127.0.0.1/panel'; RequireAck = $true; Once = $true }
            Description = '-PanelUrl http://127.0.0.1/panel -RequireAck -Once'
            Pattern = 'RequireAck is only supported'
        }
    )
    foreach ($case in $cases) {
        $rejected = $false
        $parameters = $case.Parameters
        try {
            & $bridge @parameters | Out-Null
        } catch {
            $rejected = $true
            if ($_.Exception.Message -notmatch $case.Pattern) {
                throw
            }
        }
        if (-not $rejected) {
            throw ("Expected BatteryBridge to reject arguments: {0}" -f $case.Description)
        }
    }
    $ackCases = @(
        [PSCustomObject]@{ Frame = 'BAT2,1,71,1,0'; Ack = 'ACK BAT2 1 71' },
        [PSCustomObject]@{ Frame = 'BAT2,0,-1,0,-1'; Ack = 'ACK BAT2 0 -1' },
        [PSCustomObject]@{ Frame = 'PERF,42,68,77,65,12,345,128,86400'; Ack = 'ACK PERF 42 68 77 65' }
    )
    foreach ($ackCase in $ackCases) {
        if ($ackCase.Frame -match '^BAT2,(\d+),(-?\d+),') {
            $actualAck = "ACK BAT2 $($Matches[1]) $($Matches[2])"
        } elseif ($ackCase.Frame -match '^PERF,(-?\d+),(-?\d+),(-?\d+),(-?\d+),') {
            $actualAck = "ACK PERF $($Matches[1]) $($Matches[2]) $($Matches[3]) $($Matches[4])"
        } else {
            throw ("Unable to parse ACK test frame: {0}" -f $ackCase.Frame)
        }
        if ($actualAck -ne $ackCase.Ack) {
            throw ("Unexpected ACK derivation: {0}" -f $actualAck)
        }
    }
    Write-Host ("BatteryBridge validation test passed ({0} cases, {1} ACK derivations)." -f $cases.Count, $ackCases.Count)
    return
}

$token = '0123456789abcdef0123456789abcdef'
$probe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$receiver = $null
$log = Join-Path ([IO.Path]::GetTempPath()) ('battery-bridge-smoke-' + [Guid]::NewGuid().ToString('N') + '.log')
$previousToken = [Environment]::GetEnvironmentVariable('BATTERY_PANEL_TOKEN', 'Process')

try {
    $probe.Start()
    $port = ([Net.IPEndPoint]$probe.LocalEndpoint).Port
    $probe.Stop()

    $receiver = Start-Job -ArgumentList $port, $FirstResponseError.IsPresent -ScriptBlock {
        param([int]$Port, [bool]$FailFirst)
        $ErrorActionPreference = 'Stop'
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $Port)
        $frames = @()
        try {
            $listener.Start()
            for ($index = 0; $index -lt 3; $index++) {
                $client = $listener.AcceptTcpClient()
                try {
                    $stream = $client.GetStream()
                    $stream.ReadTimeout = 5000
                    $headerBytes = [Collections.Generic.List[byte]]::new()
                    while ($true) {
                        $value = $stream.ReadByte()
                        if ($value -lt 0) { throw 'HTTP request ended before headers.' }
                        $headerBytes.Add([byte]$value)
                        $count = $headerBytes.Count
                        if ($count -ge 4 -and
                            $headerBytes[$count - 4] -eq 13 -and
                            $headerBytes[$count - 3] -eq 10 -and
                            $headerBytes[$count - 2] -eq 13 -and
                            $headerBytes[$count - 1] -eq 10) { break }
                        if ($count -gt 8192) { throw 'HTTP headers are too large.' }
                    }
                    $headers = [Text.Encoding]::ASCII.GetString($headerBytes.ToArray())
                    $lengthMatch = [Regex]::Match($headers, '(?im)^Content-Length:\s*(\d+)\s*$')
                    if (-not $lengthMatch.Success) { throw 'HTTP request has no Content-Length.' }
                    $length = [int]$lengthMatch.Groups[1].Value
                    if ($length -gt 256) { throw 'HTTP body is unexpectedly large.' }
                    $bodyBytes = New-Object byte[] $length
                    $offset = 0
                    while ($offset -lt $length) {
                        $read = $stream.Read($bodyBytes, $offset, $length - $offset)
                        if ($read -le 0) { throw 'HTTP request ended before body.' }
                        $offset += $read
                    }
                    $tokenMatch = [Regex]::Match($headers, '(?im)^X-Battery-Token:\s*(.*?)\s*$')
                    $frames += [PSCustomObject]@{
                        Body = [Text.Encoding]::UTF8.GetString($bodyBytes)
                        Token = if ($tokenMatch.Success) { $tokenMatch.Groups[1].Value } else { '' }
                    }
                    $status = if ($FailFirst -and $index -eq 0) { 500 } else { 200 }
                    $response = [Text.Encoding]::ASCII.GetBytes(("HTTP/1.1 {0} Response`r`nContent-Length: 2`r`nConnection: close`r`n`r`nOK" -f $status))
                    $stream.Write($response, 0, $response.Length)
                    $stream.Flush()
                } finally {
                    $client.Close()
                }
            }
            $frames
        } finally {
            $listener.Stop()
        }
    }

    if ($WithToken) {
        [Environment]::SetEnvironmentVariable('BATTERY_PANEL_TOKEN', $token, 'Process')
    } else {
        [Environment]::SetEnvironmentVariable('BATTERY_PANEL_TOKEN', $null, 'Process')
    }
    Start-Sleep -Milliseconds 300
    $bridgeFailed = $false
    try {
        & $bridge -PanelUrl ("http://127.0.0.1:{0}" -f $port) -Once -LogPath $log | Out-Null
    } catch {
        $bridgeFailed = $true
        if ($_.Exception.Message -like '*already running*') { throw }
        if (-not $FirstResponseError) { throw }
    }
    if ($FirstResponseError -and -not $bridgeFailed) {
        throw 'BatteryBridge unexpectedly succeeded after an HTTP 500 response.'
    }
    if (-not $FirstResponseError -and $bridgeFailed) {
        throw 'BatteryBridge failed during the normal HTTP smoke test.'
    }

    $completed = Wait-Job $receiver -Timeout 10
    if (-not $completed -or $receiver.State -ne 'Completed') {
        throw 'HTTP receiver did not receive two battery frames and one performance frame.'
    }
    $frames = @(Receive-Job $receiver)
    if ($frames.Count -ne 3) { throw ("Expected two battery frames and one performance frame, received {0}." -f $frames.Count) }
    $expectedBodies = @('BAT2,1,-1,0,-1', 'BAT2,0,-1,0,-1')
    for ($index = 0; $index -lt $expectedBodies.Count; $index++) {
        if ($frames[$index].Body -ne $expectedBodies[$index]) {
            throw ("Unexpected frame {0}: {1}" -f $index, $frames[$index].Body)
        }
        $expectedToken = if ($WithToken) { $token } else { '' }
        if ($frames[$index].Token -ne $expectedToken) {
            throw ("Unexpected token header on frame {0}." -f $index)
        }
    }
    if ($frames[2].Body -notmatch '^PERF,-?\d+,-?\d+,-?\d+,-?\d+,\d+,\d+,\d+,\d+$') {
        throw ("Unexpected performance frame: {0}" -f $frames[2].Body)
    }
    if ($frames[2].Token -ne $(if ($WithToken) { $token } else { '' })) {
        throw 'Unexpected token header on performance frame.'
    }
    if (-not (Test-Path -LiteralPath $log)) { throw 'BatteryBridge did not create the transcript log.' }
    $logText = Get-Content -LiteralPath $log -Raw
    if ($logText.Contains($token)) { throw 'BatteryBridge transcript leaked the token.' }
    Write-Host ("BatteryBridge HTTP smoke test passed ({0} frames including PERF, token={1}, firstResponseError={2})." -f $frames.Count, $WithToken.IsPresent, $FirstResponseError.IsPresent)
} finally {
    [Environment]::SetEnvironmentVariable('BATTERY_PANEL_TOKEN', $previousToken, 'Process')
    if ($receiver) {
        if ($receiver.State -eq 'Running') { Stop-Job $receiver -ErrorAction SilentlyContinue }
        Remove-Job $receiver -Force -ErrorAction SilentlyContinue
    }
    if ($probe) { $probe.Stop() }
    Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue
}
