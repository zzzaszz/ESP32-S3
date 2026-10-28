param([string[]]$Paths)
$ErrorActionPreference = 'Stop'
if (-not ('HyperXReader' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'HyperXReader.cs') }
if (-not $PSBoundParameters.ContainsKey('Paths')) {
    $Paths = @(Get-PnpDevice -PresentOnly -Class HIDClass |
    Where-Object InstanceId -like 'HID\VID_03F0&PID_02CC*' |
    ForEach-Object { '\\?\' + $_.InstanceId.Replace('\','#') + '#{4d1e55b2-f16f-11cf-88cb-001111000030}' })
}
[HyperXReader]::Poll([string[]]$Paths)
