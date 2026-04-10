try {
    $p = New-Object System.IO.Ports.SerialPort("COM3")
    $p.Open()
    Write-Host "OPEN OK"
    $p.Close()
} catch {
    Write-Host ("BUSY: " + $_.Exception.Message)
}
