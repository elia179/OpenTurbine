param(
  [string]$OutputDirectory = "$env:LOCALAPPDATA\OpenTurbine\SetupTool\logs"
)

$ErrorActionPreference = "Continue"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$root = Join-Path $OutputDirectory "driver-diagnostics-$stamp"
New-Item -ItemType Directory -Force -Path $root | Out-Null

function Write-CommandLog {
  param(
    [string]$Name,
    [string]$FileName,
    [string[]]$Arguments
  )
  $out = Join-Path $root "$Name.txt"
  "Command: $FileName $($Arguments -join ' ')" | Set-Content -Encoding UTF8 $out
  try {
    & $FileName @Arguments *>> $out
    "ExitCode: $LASTEXITCODE" | Add-Content -Encoding UTF8 $out
  } catch {
    "Error: $($_.Exception.Message)" | Add-Content -Encoding UTF8 $out
  }
}

Get-ComputerInfo | Out-File -Encoding UTF8 (Join-Path $root "computer-info.txt")
Get-ChildItem Env: | Sort-Object Name | Out-File -Encoding UTF8 (Join-Path $root "environment.txt")
Write-CommandLog -Name "pnputil-connected-ids" -FileName "$env:WINDIR\System32\pnputil.exe" -Arguments @("/enum-devices", "/connected", "/ids")
Write-CommandLog -Name "pnputil-connected-drivers" -FileName "$env:WINDIR\System32\pnputil.exe" -Arguments @("/enum-devices", "/connected", "/drivers")
Write-CommandLog -Name "serialcomm-registry" -FileName "$env:WINDIR\System32\reg.exe" -Arguments @("query", "HKLM\HARDWARE\DEVICEMAP\SERIALCOMM")

$setupApi = Join-Path $env:WINDIR "INF\setupapi.dev.log"
if (Test-Path $setupApi) {
  Get-Content $setupApi -Tail 1200 | Set-Content -Encoding UTF8 (Join-Path $root "setupapi.dev.tail.txt")
}

$zip = Join-Path $OutputDirectory "driver-diagnostics-$stamp.zip"
if (Test-Path $zip) {
  Remove-Item -LiteralPath $zip -Force
}
Compress-Archive -Path (Join-Path $root "*") -DestinationPath $zip -Force
Write-Host $zip
