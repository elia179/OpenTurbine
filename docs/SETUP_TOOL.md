# OpenTurbine Setup Tool

The Windows setup tool lets users install a blank board over USB or update an
existing OpenTurbine board over Wi-Fi. Users download only:

```text
OpenTurbineSetupTool.exe
```

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

Then create the release ZIP:

```bash
python tools/build_setup_package.py --esptool C:\path\to\esptool.exe
```

Optional driver installers can be included for the setup tool's Driver Help
screen:

```bash
python tools/build_setup_package.py ^
  --esptool C:\path\to\esptool.exe ^
  --cp210x-driver C:\path\to\CP210xVCPInstaller_x64.exe ^
  --ch340-driver C:\path\to\CH341SER.EXE
```

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

Driver installers under `drivers/` are optional but recommended.

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
