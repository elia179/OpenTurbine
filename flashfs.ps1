$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$out = "$env:TEMP\pio_fs_out.txt"
$err = "$env:TEMP\pio_fs_err.txt"
$proc = Start-Process `
  -FilePath 'C:\Users\elial\.platformio\penv\Scripts\pio.exe' `
  -ArgumentList 'run','-t','uploadfs' `
  -WorkingDirectory 'C:\Users\elial\Documents\Dev\OpenTurbine' `
  -Wait -PassThru -NoNewWindow `
  -RedirectStandardOutput $out `
  -RedirectStandardError $err
Get-Content $out -ErrorAction SilentlyContinue
Get-Content $err -ErrorAction SilentlyContinue
exit $proc.ExitCode
