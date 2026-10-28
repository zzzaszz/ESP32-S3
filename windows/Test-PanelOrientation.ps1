param([string]$Port = 'COM12')
$ErrorActionPreference = 'Stop'
# Requires exclusive serial access. No fake IMU values are fed to the live driver.
$serial = [IO.Ports.SerialPort]::new($Port, 115200)
$serial.Encoding = [Text.Encoding]::UTF8
$serial.DtrEnable = $true
$serial.NewLine = "`n"
$serial.WriteTimeout = 3000
function Send-OrientationCommand([string]$Command) {
    $serial.WriteLine($Command)
    Start-Sleep -Milliseconds 250
    $reply = $serial.ReadExisting()
    Write-Host ($Command + ' => ' + $reply.Trim())
    return $reply
}
function Assert-Orientation([string]$Reply, [string]$Pattern) {
    if ($Reply -notmatch $Pattern) { throw "Expected $Pattern, received $Reply" }
}
try {
    $serial.Open()
    Start-Sleep -Milliseconds 2000
    Write-Host $serial.ReadExisting()
    Assert-Orientation (Send-OrientationCommand 'ORIENTATION TEST') 'ORIENTATION TEST PASS'
    Assert-Orientation (Send-OrientationCommand 'ORIENTATION NORMAL') 'mode=NORMAL reversed=0'
    Assert-Orientation (Send-OrientationCommand 'UI?') 'screen=320x240.*boundsErrors=0'
    Assert-Orientation (Send-OrientationCommand 'ORIENTATION REVERSED') 'mode=REVERSED reversed=1'
    Assert-Orientation (Send-OrientationCommand 'UI?') 'screen=320x240.*boundsErrors=0'
    Assert-Orientation (Send-OrientationCommand 'HEALTH?') '(?m)^reversed=1\r?$'
    Assert-Orientation (Send-OrientationCommand 'ORIENTATION INVALID') 'ERR ORIENTATION FORMAT'
    Assert-Orientation (Send-OrientationCommand 'ORIENTATION?') 'mode=REVERSED reversed=1'
    Assert-Orientation (Send-OrientationCommand 'ORIENTATION INVERT ON') 'mode=REVERSED reversed=1 invert=1'
    Assert-Orientation (Send-OrientationCommand 'ORIENTATION INVERT OFF') 'mode=REVERSED reversed=1 invert=0'
    Assert-Orientation (Send-OrientationCommand 'ORIENTATION AUTO') 'mode=AUTO'
    Start-Sleep -Seconds 2
    $state = Send-OrientationCommand 'ORIENTATION?'
    Assert-Orientation $state 'sensor=ready.*valid=1.*errors=0'
    Assert-Orientation $state 'algorithm=accel\+gyro.*gz=.*yaw='
    Assert-Orientation (Send-OrientationCommand 'UI?') 'screen=320x240.*boundsErrors=0'
    Write-Host 'Orientation policy, touch corner mapping, commands and actual QMI8658 reads passed.'
    Write-Host 'Physical rotation, correct sensor polarity and gesture direction still require moving the board.'
} finally {
    if ($serial.IsOpen) {
        $serial.WriteLine('ORIENTATION INVERT OFF')
        $serial.WriteLine('ORIENTATION AUTO')
        Start-Sleep -Milliseconds 200
    }
    $serial.Dispose()
}
