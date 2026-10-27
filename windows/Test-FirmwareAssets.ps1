$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$dashboardPath = Join-Path $repo 'BatteryDashboard.cpp'
$fontPath = Join-Path $repo 'battery_font_16.c'
$dashboard = Get-Content -LiteralPath $dashboardPath -Raw -Encoding utf8
$font = Get-Content -LiteralPath $fontPath -Raw -Encoding utf8

$needed = @([regex]::Matches($dashboard, '[\u3400-\u9fff]') |
    ForEach-Object Value | Sort-Object -Unique)
$glyphs = @([regex]::Matches($font, '/\* U\+([0-9A-Fa-f]+)') |
    ForEach-Object {
        [char]::ConvertFromUtf32([Convert]::ToInt32($_.Groups[1].Value, 16))
    } | Sort-Object -Unique)
$missing = @($needed | Where-Object { $glyphs -notcontains $_ })
if ($missing.Count) {
    throw ("Font is missing {0} dashboard characters: {1}" -f $missing.Count, ($missing -join ''))
}

Write-Host ("Firmware asset check passed (dashboardChinese={0}, fontGlyphs={1}, missing={2})." -f
    $needed.Count, $glyphs.Count, $missing.Count)
