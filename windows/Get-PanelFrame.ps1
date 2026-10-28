param(
    [string]$Port = 'COM12',
    [ValidateSet(1, 2)][int]$Page = 2,
    [switch]$KeepPage,
    [string]$OutputPath = (Join-Path $env:TEMP 'battery-panel-render.png')
)
$ErrorActionPreference = 'Stop'
# Captures the actual LVGL flush buffer, not a photo of the LCD glass.
# Stop BatteryBridge before running; no serial port is shared with the bridge.
Add-Type -AssemblyName System.Drawing
$serial = [IO.Ports.SerialPort]::new($Port, 115200)
$serial.Encoding = [Text.Encoding]::UTF8
$serial.DtrEnable = $true
$serial.NewLine = "`n"
$serial.WriteTimeout = 3000
try {
    $serial.Open()
    Start-Sleep -Milliseconds 400
    $null = $serial.ReadExisting()
    if (-not $KeepPage) {
        $serial.WriteLine("PAGE $Page")
        Start-Sleep -Milliseconds 300
        $null = $serial.ReadExisting()
    }
    $serial.WriteLine('FRAME?')
    $reply = [Text.StringBuilder]::new()
    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    do {
        $null = $reply.Append($serial.ReadExisting())
        if ($reply.ToString().Contains('FRAME END') -or $reply.ToString().Contains('ERR FRAME')) { break }
        Start-Sleep -Milliseconds 20
    } while ([DateTime]::UtcNow -lt $deadline)
    $text = $reply.ToString()
    if ($text -notmatch '(?s)FRAME BEGIN 160 120\r?\n(.*?)FRAME END') {
        throw ('Render capture failed: ' + $text.Substring(0, [Math]::Min(300, $text.Length)))
    }
    $rows = @($Matches[1] -split '\r?\n' | Where-Object { $_ -match '^[0-9A-F]{640}$' })
    if ($rows.Count -ne 120) { throw "Expected 120 rendered rows, got $($rows.Count)." }
    $bitmap = [Drawing.Bitmap]::new(160, 120)
    $preview = $null; $graphics = $null
    $bright = 0; $colors = [Collections.Generic.HashSet[int]]::new()
    try {
        for ($y = 0; $y -lt 120; $y++) {
            for ($x = 0; $x -lt 160; $x++) {
                $color = [Convert]::ToUInt16($rows[$y].Substring($x * 4, 4), 16)
                $r = [int](($color -shr 11) -band 31) * 255 / 31
                $g = [int](($color -shr 5) -band 63) * 255 / 63
                $b = [int]($color -band 31) * 255 / 31
                $bitmap.SetPixel($x, $y, [Drawing.Color]::FromArgb([int]$r, [int]$g, [int]$b))
                $null = $colors.Add([int]$color)
                if ($r + $g + $b -gt 400) { $bright++ }
            }
        }
        $preview = [Drawing.Bitmap]::new(480, 360)
        $graphics = [Drawing.Graphics]::FromImage($preview)
        $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::Half
        $graphics.DrawImage($bitmap, [Drawing.Rectangle]::new(0, 0, 480, 360))
        $preview.Save($OutputPath, [Drawing.Imaging.ImageFormat]::Png)
        [PSCustomObject]@{ Page = $Page; Colors = $colors.Count; BrightPixels = $bright; Output = $OutputPath }
    } finally {
        if ($graphics) { $graphics.Dispose() }
        if ($preview) { $preview.Dispose() }
        $bitmap.Dispose()
    }
} finally { $serial.Dispose() }
