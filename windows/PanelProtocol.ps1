function Read-PanelOutput($Serial) {
    $script:panelLineBuffer += $Serial.ReadExisting()
    $lines = [Collections.Generic.List[string]]::new()
    while (($end = $script:panelLineBuffer.IndexOf("`n")) -ge 0) {
        $line = $script:panelLineBuffer.Substring(0, $end).TrimEnd("`r")
        $script:panelLineBuffer = $script:panelLineBuffer.Substring($end + 1)
        if ($line -match '^REQUEST BATTERY \d+$') { $script:refreshRequested = $true }
        $lines.Add($line)
    }
    if ($script:panelLineBuffer.Length -gt 4096) { $script:panelLineBuffer = '' }
    return $lines.ToArray()
}

function Get-PanelAck([string]$Frame) {
    if ($Frame -match '^TIME,(\d+)$') { return "ACK TIME $($Matches[1])" }
    if ($Frame -match '^BAT2,(\d+),(-?\d+),') { return "ACK BAT2 $($Matches[1]) $($Matches[2])" }
    if ($Frame -match '^PERF,(-?\d+),(-?\d+),(-?\d+),(-?\d+),') {
        return "ACK PERF $($Matches[1]) $($Matches[2]) $($Matches[3]) $($Matches[4])"
    }
    throw 'Unable to derive expected ACK.'
}

function Test-PanelAck([string]$Reply, [string]$Expected) {
    return @($Reply -split '\r?\n' | Where-Object { $_ -ceq $Expected }).Count -gt 0
}

function Convert-GHubBattery($Battery, [string]$DeviceId) {
    $level = -1; $charging = -1
    if ($Battery.deviceId -ne $DeviceId) { throw 'G HUB battery response deviceId mismatch.' }
    # A missing JSON percentage must never become 0 through PowerShell conversion.
    if ($null -ne $Battery.percentage -and $Battery.percentage -isnot [bool] -and
        [string]$Battery.percentage -match '^\d+(\.\d+)?$' -and -not $Battery.isBatteryRemoved) {
        $value = [double]$Battery.percentage
        if ($value -ge 0 -and $value -le 100) { $level = [int][Math]::Round($value) }
    }
    if ($Battery.charging -is [bool]) { $charging = [int]$Battery.charging }
    [PSCustomObject]@{ Level = $level; Charging = $charging }
}
