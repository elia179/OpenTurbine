# OpenTurbine Setup Tool

The Windows setup tool provides two deliberately distinct paths: **Clean install
/ reinstall** erases a blank or previously used board over USB, while **Update
and keep my setup** updates an existing OpenTurbine board over Wi-Fi without a
factory reset. Users download only:

```text
OpenTurbineSetupTool.exe
```

The authoritative user installation and operating guide is the repository root
[`README.md`](../README.md). Focused browser and SmartScreen troubleshooting is
in [`WINDOWS_FLASHER_INSTALL.md`](WINDOWS_FLASHER_INSTALL.md). The rest of this
file is for setup-tool developers and release packagers.

The CP210x button downloads Silicon Labs' complete CP210x Universal Windows
Driver v11.5.0 directly from the official Silicon Labs URL, verifies a pinned
SHA-256, and installs its signed INF/CAT package through Windows `pnputil`.
Driver installation therefore works even if an older firmware package contains
only the legacy DPInst EXE, while the OpenTurbine EXE remains fully open source.

On launch, the app looks for a local `OpenTurbine_Recommended.zip` next to the
EXE first. If it is not there, it downloads this release asset:

```text
https://github.com/elia179/OpenTurbine/releases/latest/download/OpenTurbine_Recommended.zip
```

Publish `OpenTurbine_Recommended.zip.sha256` beside it so the tool can verify
the download.

## Firmware Support

Current firmware exposes:

```http
GET /api/device_info
```

This endpoint reports the board target (`esp32dev` or `esp32s3dev`), chip name,
firmware version, current state, whether outputs are active, and whether OTA is
currently allowed. The setup tool uses it during Wi-Fi updates when a package
contains both ESP32 and ESP32-S3 firmware.

## Build The Recommended Package

Build both firmware targets and their LittleFS images:

```bash
pio run -e esp32dev
pio run -e esp32dev -t buildfs
pio run -e esp32s3dev
pio run -e esp32s3dev -t buildfs
```

Then create the release ZIP with both extracted driver packages:

```bash
python tools/build_setup_package.py ^
  --esptool C:\path\to\esptool.exe ^
  --cp210x-driver C:\path\to\extracted\CP210x_Windows_Drivers ^
  --ch340-driver C:\path\to\extracted\wch-serial-drivers
```

Pass the extracted vendor folder, not a copied installer EXE. In particular,
`CP210xVCPInstaller_x64.exe` is DPInst and cannot install anything unless its
`.inf`, `.cat`, and driver files remain beside it:

For WCH bridges, the setup tool accepts either the vendor installer EXE or
signed driver folders containing `.inf`, `.cat`, and `.sys` files. A package can
include both CH340/CH341 and CH343/CH910x driver folders under
`drivers/ch340/`; the setup tool installs matching devices with Windows
`pnputil`.

For local packaging tests only, `--allow-missing-drivers` bypasses the release
requirement. Never publish a package built with that flag.

The script writes:

```text
dist/setup_tool/OpenTurbine_Recommended.zip
dist/setup_tool/OpenTurbine_Recommended.zip.sha256
```

## Release Checklist

Attach these assets to the GitHub release:

```text
OpenTurbineSetupTool.exe
OpenTurbineSetupTool.exe.sha256
OpenTurbine_Recommended.zip
OpenTurbine_Recommended.zip.sha256
```

## Code Signing

Public Windows releases should be Authenticode-signed before publishing. An
unsigned or new low-reputation EXE can trigger Microsoft Defender SmartScreen,
browser download warnings, or Windows 11 Smart App Control. A signature does not
guarantee that Microsoft will immediately stop warning on a brand-new app, but it
gives Windows a verified publisher identity and lets reputation carry forward
across releases signed by the same publisher.

Use a production OV/EV code-signing certificate issued by a CA trusted by
Windows. For local signing from a PFX:

```powershell
$env:WINDOWS_SIGNING_CERT_PASSWORD = "pfx-password"
.\tools\sign_windows_setup_tool.ps1 `
  -ExePath .\dist\setup_tool\OpenTurbineSetupTool.exe `
  -CertificatePath C:\secure\OpenTurbineCodeSigning.pfx `
  -CertificatePassword $env:WINDOWS_SIGNING_CERT_PASSWORD
```

For a certificate already installed in the Windows certificate store, or on a
local hardware token exposed through the certificate store:

```powershell
.\tools\sign_windows_setup_tool.ps1 `
  -ExePath .\dist\setup_tool\OpenTurbineSetupTool.exe `
  -CertificateThumbprint "certificate-thumbprint"
```

Generate `OpenTurbineSetupTool.exe.sha256` only after signing:

```powershell
$hash = (Get-FileHash .\dist\setup_tool\OpenTurbineSetupTool.exe -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  OpenTurbineSetupTool.exe" | Set-Content -Encoding ascii .\dist\setup_tool\OpenTurbineSetupTool.exe.sha256
Get-AuthenticodeSignature .\dist\setup_tool\OpenTurbineSetupTool.exe
```

GitHub Actions can sign the setup tool automatically when these repository
secrets are configured:

```text
WINDOWS_SIGNING_CERT_BASE64   base64-encoded PFX file
WINDOWS_SIGNING_CERT_PASSWORD PFX password
```

The CI job intentionally skips signing when those secrets are absent, so pull
requests and local forks can still build. The Windows job uploads
`OpenTurbineSetupTool` as a workflow artifact containing the EXE and checksum;
attach those files to the GitHub release after confirming the EXE is signed. If
your production certificate uses a cloud HSM, Azure Trusted Signing, or a USB
token that cannot be exported as a PFX, run that provider's signing step before
the checksum step and keep the same publish rule: sign first, hash second.

References:

- Microsoft SmartScreen reputation for Windows app developers: https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation
- Microsoft Smart App Control overview: https://learn.microsoft.com/en-us/windows/apps/develop/smart-app-control/overview
- Microsoft SignTool reference: https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool

The ZIP must contain:

```text
manifest.json
tools/esptool.exe
drivers/cp210x/CP210xVCPInstaller_x64.exe
drivers/cp210x/*.inf
drivers/cp210x/*.cat
drivers/cp210x/*.(driver payload files)
drivers/ch340/**/CH341SER.INF
drivers/ch340/**/CH341SER.CAT
drivers/ch340/**/CH341*.SYS
drivers/ch340/**/CH343SER.INF
drivers/ch340/**/CH343SER.CAT
drivers/ch340/**/CH343*.SYS
esp32dev/bootloader.bin
esp32dev/partitions.bin
esp32dev/boot_app0.bin
esp32dev/firmware.bin
esp32dev/littlefs.bin
esp32dev/web_assets/*.gz
esp32s3dev/bootloader.bin
esp32s3dev/partitions.bin
esp32s3dev/boot_app0.bin
esp32s3dev/firmware.bin
esp32s3dev/littlefs.bin
esp32s3dev/web_assets/*.gz
```

Do not publish a CP210x installer without its adjacent driver payload. The setup
tool rejects that incomplete package instead of opening an installer that cannot
install a driver.

Recommended driver sources:

- CP210x: Silicon Labs CP210x USB to UART Bridge VCP Drivers.
- CH340/CH341/CH343: WCH CH341SER Windows USB serial driver.

## Local Tryout

Before publishing a release, place a real `OpenTurbine_Recommended.zip` next to
`OpenTurbineSetupTool.exe` and double-click the EXE. The app will use the local
package first.

Use a blank or sacrificial board for the first USB install test. For Wi-Fi
updates, the tool backs up `ecu_config.json` into:

```text
Documents\OpenTurbine\Backups
```

Treat backups as private because they can contain the board Wi-Fi password.
