param([string]$Port = 'COM12')
$ErrorActionPreference = 'Stop'
# Exclusive serial access; all percentages below are synthetic rendering fixtures.
$outputDirectory = Join-Path $env:TEMP ('battery-render-test-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $outputDirectory
function Send-RenderCommand([string]$Command) {
    $serial = [IO.Ports.SerialPort]::new($Port, 115200)
    $serial.Encoding = [Text.Encoding]::UTF8
    $serial.DtrEnable = $true
    $serial.NewLine = "`n"
    try {
        $serial.Open()
        $null = $serial.ReadExisting()
        $serial.WriteLine($Command)
        Start-Sleep -Milliseconds 300
        $reply = $serial.ReadExisting()
        Write-Host ($Command + ' => ' + $reply.Trim())
        return $reply
    } finally { $serial.Dispose() }
}
function Save-Render([int]$Page, [string]$Name) {
    $capture = & (Join-Path $PSScriptRoot 'Get-PanelFrame.ps1') -Port $Port -Page $Page -KeepPage -OutputPath (Join-Path $outputDirectory $Name)
    if ($capture.BrightPixels -lt 180 -or $capture.Colors -lt 30) {
        throw "Page $Page appears blank: bright=$($capture.BrightPixels), colors=$($capture.Colors)."
    }
    Write-Host ("Rendered page {0}: bright={1}, colors={2}, image={3}" -f $Page, $capture.BrightPixels, $capture.Colors, $capture.Output)
}
try {
    $null = Send-RenderCommand 'ORIENTATION NORMAL'
    $null = Send-RenderCommand 'PERF,42,68,77,65,12,345,128,86400,55'
    $null = Send-RenderCommand 'BAT2,0,93,1,0'
    $null = Send-RenderCommand 'BAT2,1,63,1,0'
    $null = Send-RenderCommand 'PAGE 1'
    Save-Render 1 'normal-battery.png'
    $null = Send-RenderCommand 'SWIPE LEFT'
    $ui = Send-RenderCommand 'UI?'
    if ($ui -notmatch 'page=1.*boundsErrors=0' -or $ui -notmatch 'sliding=0 fontBitmap=ready' -or $ui -notmatch 'monitorVisible=1') {
        throw 'Monitor did not settle after animated swipe.'
    }
    Save-Render 2 'normal-monitor.png'
    $null = Send-RenderCommand 'ORIENTATION REVERSED'
    Save-Render 2 'reversed-monitor.png'
    $null = Send-RenderCommand 'SWIPE RIGHT'
    $ui = Send-RenderCommand 'UI?'
    if ($ui -notmatch 'page=0.*boundsErrors=0' -or $ui -notmatch 'sliding=0 fontBitmap=ready') {
        throw 'Battery page did not settle after reverse-direction swipe.'
    }
    Save-Render 1 'reversed-battery.png'
    # Repeat animation and rotation without recreating objects or replacing data.
    for ($i = 0; $i -lt 6; $i++) {
        $null = Send-RenderCommand $(if ($i % 2) { 'SWIPE RIGHT' } else { 'SWIPE LEFT' })
        $ui = Send-RenderCommand 'UI?'
        if ($ui -notmatch 'boundsErrors=0' -or $ui -notmatch 'sliding=0 fontBitmap=ready') { throw 'Repeated swipe failed.' }
    }
    Write-Host "Render-buffer, font, animated navigation and both rotation states passed. Images: $outputDirectory"
    Write-Host 'This verifies emitted pixels; it is not an LCD photo or a physical gesture test.'
} finally {
    $null = Send-RenderCommand 'BAT2,0,-1,0,-1'
    $null = Send-RenderCommand 'BAT2,1,-1,0,-1'
    $null = Send-RenderCommand 'PERF,-1,-1,-1,-1,0,0,0,0'
    $null = Send-RenderCommand 'ORIENTATION AUTO'
}
