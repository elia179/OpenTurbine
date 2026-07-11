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
  --ch340-driver C:\path\to\extracted\CH341SER
```

Pass the extracted vendor folder, not a copied installer EXE. In particular,
`CP210xVCPInstaller_x64.exe` is DPInst and cannot install anything unless its
`.inf`, `.cat`, and driver files remain beside it:

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
OpenTurbine_Recommended.zip
OpenTurbine_Recommended.zip.sha256
```

The ZIP must contain:

```text
manifest.json
tools/esptool.exe
drivers/cp210x/CP210xVCPInstaller_x64.exe
drivers/cp210x/*.inf
drivers/cp210x/*.cat
drivers/cp210x/*.(driver payload files)
drivers/ch340/CH341SER.EXE
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
