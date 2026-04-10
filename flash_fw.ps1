$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"
& "C:\Users\elial\.platformio\penv\Scripts\pio.exe" run -e esp32dev -t upload
