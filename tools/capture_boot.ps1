# Captures serial boot log from the device on a given COM port.
# Usage: powershell -File tools\capture_boot.ps1 -Port COM7 [-Seconds 12]
param([string]$Port = "COM7", [int]$Seconds = 12)

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, None, 8, one
$sp.ReadTimeout = 500
$sp.Open()

# Toggle reset through the CH340 auto-reset circuit
$sp.DtrEnable = $false; $sp.RtsEnable = $true
Start-Sleep -Milliseconds 100
$sp.RtsEnable = $false
Start-Sleep -Milliseconds 50
$sp.DtrEnable = $true

$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
    try {
        $line = $sp.ReadLine()
        if ($line) { Write-Output $line }
    } catch [TimeoutException] {}
}
$sp.Close()
