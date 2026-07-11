[CmdletBinding(DefaultParameterSetName = "Pfx")]
param(
    [Parameter(Mandatory = $true)]
    [string]$ExePath,

    [Parameter(Mandatory = $true, ParameterSetName = "Pfx")]
    [string]$CertificatePath,

    [Parameter(ParameterSetName = "Pfx")]
    [string]$CertificatePassword = "",

    [Parameter(Mandatory = $true, ParameterSetName = "Thumbprint")]
    [string]$CertificateThumbprint,

    [Parameter(Mandatory = $true, ParameterSetName = "Subject")]
    [string]$CertificateSubjectName,

    [string]$TimestampUrl = "http://timestamp.digicert.com",

    [string]$SignToolPath
)

$ErrorActionPreference = "Stop"

function Resolve-SignTool {
    if ($SignToolPath) {
        if (Test-Path -LiteralPath $SignToolPath) {
            return (Resolve-Path -LiteralPath $SignToolPath).Path
        }
        throw "signtool.exe was not found at '$SignToolPath'."
    }

    $fromPath = Get-Command signtool.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($fromPath) {
        return $fromPath.Source
    }

    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    if (Test-Path -LiteralPath $kitsRoot) {
        $candidate = Get-ChildItem -Path $kitsRoot -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\x64\\signtool\.exe$" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }

    throw "signtool.exe was not found. Install the Windows SDK or pass -SignToolPath."
}

if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "EXE was not found at '$ExePath'."
}

$resolvedExe = (Resolve-Path -LiteralPath $ExePath).Path
$signtool = Resolve-SignTool

$signArgs = @("sign")
switch ($PSCmdlet.ParameterSetName) {
    "Pfx" {
        if (-not (Test-Path -LiteralPath $CertificatePath)) {
            throw "Certificate PFX was not found at '$CertificatePath'."
        }
        $resolvedCert = (Resolve-Path -LiteralPath $CertificatePath).Path
        $signArgs += @("/f", $resolvedCert)
        if (-not [string]::IsNullOrEmpty($CertificatePassword)) {
            $signArgs += @("/p", $CertificatePassword)
        }
    }
    "Thumbprint" {
        $signArgs += @("/sha1", $CertificateThumbprint)
    }
    "Subject" {
        $signArgs += @("/n", $CertificateSubjectName)
    }
}

$signArgs += @("/fd", "SHA256", "/tr", $TimestampUrl, "/td", "SHA256", $resolvedExe)

Write-Host "Signing $resolvedExe"
& $signtool @signArgs
if ($LASTEXITCODE -ne 0) {
    throw "signtool sign failed with exit code $LASTEXITCODE."
}

Write-Host "Verifying signature"
& $signtool verify /pa /v $resolvedExe
if ($LASTEXITCODE -ne 0) {
    throw "signtool verify failed with exit code $LASTEXITCODE."
}
