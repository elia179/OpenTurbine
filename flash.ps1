[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
Set-Location 'C:\Users\elial\Documents\Dev\OpenTurbine'
Write-Host '=== Flashing firmware ==='
& 'C:\Users\elial\.platformio\penv\Scripts\pio.exe' run -e esp32dev -t upload
Write-Host '=== Flashing filesystem ==='
& 'C:\Users\elial\.platformio\penv\Scripts\pio.exe' run -e esp32dev -t uploadfs
Write-Host '=== All done ==='
