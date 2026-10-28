$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$dashboardPath = Join-Path $repo 'BatteryDashboard.cpp'
$fontPath = Join-Path $repo 'battery_font_12.c'
$dashboard = Get-Content -LiteralPath $dashboardPath -Raw -Encoding utf8
$font = Get-Content -LiteralPath $fontPath -Raw -Encoding utf8
$configuration = Get-Content -LiteralPath (Join-Path $repo 'lv_conf.h') -Raw -Encoding utf8
$format = [regex]::Match($font, '\.bitmap_format\s*=\s*(\d+)')
$compression = [regex]::Match($configuration, '(?m)^\s*#define\s+LV_USE_FONT_COMPRESSED\s+(\d+)')
if (-not $format.Success -or -not $compression.Success) { throw 'Unable to verify font bitmap format against LVGL configuration.' }
if ([int]$format.Groups[1].Value -ne 0 -and [int]$compression.Groups[1].Value -eq 0) {
    throw 'Custom font is compressed but LV_USE_FONT_COMPRESSED=0: labels will be invisible.'
}

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
Write-Host ("Font rendering compatibility passed (bitmap_format={0}, decompression={1})." -f $format.Groups[1].Value, $compression.Groups[1].Value)
