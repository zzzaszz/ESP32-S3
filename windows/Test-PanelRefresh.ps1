$ErrorActionPreference = 'Stop'
# Integration test: fake panel, real read-only device queries. No ESP32 required.
# Do not run alongside BatteryBridge or the offline suite (shared bridge mutex).
$bridge = Join-Path $PSScriptRoot 'BatteryBridge.ps1'
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$job = $null
$pidFile = Join-Path ([IO.Path]::GetTempPath()) 'battery-bridge-esp32-s3.pid'
$stopFile = $pidFile + '.stop'
$healthCount = 0; $postCount = 0; $firstBatchAt = $null; $secondBatchAt = $null
if (Test-Path -LiteralPath $pidFile) { throw 'Stop BatteryBridge before this integration test.' }
try {
    $listener.Start()
    $port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port
    $job = Start-Job -ArgumentList $bridge, $port -ScriptBlock {
        param($Bridge, $Port)
        & $Bridge -PanelUrl "http://127.0.0.1:$Port" -IntervalSeconds 60
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    while ($postCount -lt 6 -and [DateTime]::UtcNow -lt $deadline) {
        if (-not $listener.Pending()) { Start-Sleep -Milliseconds 50; continue }
        $client = $listener.AcceptTcpClient()
        try {
            $stream = $client.GetStream(); $stream.ReadTimeout = 5000
            $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII, $false, 1024, $true)
            $request = $reader.ReadLine()
            $length = 0; $headerCount = 0
            while (($line = $reader.ReadLine())) {
                if ($line -match '^Content-Length:\s*(\d+)$') { $length = [int]$Matches[1] }
                if (++$headerCount -gt 100) { throw 'Unexpected HTTP headers.' }
            }
            if ($request -like 'GET /health *') {
                ++$healthCount
                $sequence = if ($healthCount -ge 2) { 1 } else { 0 }
                $body = "BatteryPanel/2`nrefresh=$sequence`nscreen=320x240`n"
            } elseif ($request -like 'POST /battery *') {
                if ($length -lt 1 -or $length -gt 128) { throw 'Invalid test frame length.' }
                $buffer = New-Object char[] $length
                $offset = 0
                while ($offset -lt $length) {
                    $count = $reader.Read($buffer, $offset, $length - $offset)
                    if ($count -le 0) { throw 'Truncated HTTP frame.' }
                    $offset += $count
                }
                $frame = -join $buffer
                if ($frame -notmatch '^(BAT2|PERF),') { throw 'Unexpected panel frame.' }
                ++$postCount
                if ($postCount -eq 3) { $firstBatchAt = [DateTime]::UtcNow }
                if ($postCount -eq 4) { $secondBatchAt = [DateTime]::UtcNow }
                Write-Host ("POST {0}: {1}" -f $postCount, $frame)
                $body = 'OK'
            } else { throw "Unexpected request: $request" }
            $bodyBytes = [Text.Encoding]::ASCII.GetBytes($body)
            $headerBytes = [Text.Encoding]::ASCII.GetBytes("HTTP/1.1 200 OK`r`nContent-Type: text/plain`r`nContent-Length: $($bodyBytes.Length)`r`nConnection: close`r`n`r`n")
            $stream.Write($headerBytes, 0, $headerBytes.Length)
            $stream.Write($bodyBytes, 0, $bodyBytes.Length)
            $stream.Flush()
        } finally { $client.Dispose() }
    }
    if ($postCount -ne 6) { throw 'Manual refresh did not cause a second batch.' }
    Set-Content -LiteralPath $stopFile -Value 'stop' -Encoding ascii
    if (-not (Wait-Job $job -Timeout 15)) { throw 'Bridge did not stop gracefully.' }
    $output = Receive-Job $job 6>&1 | Out-String
    if ($job.State -ne 'Completed' -or $output -notmatch 'Panel requested immediate battery read') {
        throw "Bridge refresh handling failed: $output"
    }
    $elapsed = ($secondBatchAt - $firstBatchAt).TotalSeconds
    if ($elapsed -ge 60) { throw 'Second batch waited for the periodic interval.' }
    if ((Test-Path -LiteralPath $pidFile) -or (Test-Path -LiteralPath $stopFile)) { throw 'Bridge did not clean up.' }
    Write-Host $output
    Write-Host ("Manual refresh integration passed: health requests={0}, frames={1}, next batch={2:N2}s (period=60s)." -f $healthCount, $postCount, $elapsed)
    Write-Host 'This tests the bridge with a loopback panel; physical Wi-Fi and touch remain separate.'
} finally {
    $listener.Stop()
    if ($job) {
        if ($job.State -eq 'Running') {
            Set-Content -LiteralPath $stopFile -Value 'stop' -Encoding ascii
            $null = Wait-Job $job -Timeout 15
            if ($job.State -eq 'Running') { Stop-Job $job }
        }
        Remove-Job $job -Force
    }
}
