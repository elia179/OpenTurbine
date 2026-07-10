# OpenTurbine Setup Tool

Windows setup/update helper for OpenTurbine boards.

Build from this directory:

```powershell
go test ./...
go build -ldflags="-H windowsgui -s -w" -o OpenTurbineSetupTool.exe .
```

The app downloads `OpenTurbine_Recommended.zip` from the latest GitHub release,
or uses a local `OpenTurbine_Recommended.zip` placed next to the EXE.

Build the recommended ZIP from the repository root:

```powershell
python tools/build_setup_package.py `
  --esptool "$env:USERPROFILE\.platformio\penv\Scripts\esptool.exe" `
  --cp210x-driver C:\path\to\CP210xVCPInstaller_x64.exe `
  --ch340-driver C:\path\to\CH341SER.EXE
```

Driver installers are optional but recommended. CP210x covers Silicon Labs USB
serial boards. CH340 covers WCH CH340, CH341, and CH343 USB serial boards.

Release assets:

```text
OpenTurbineSetupTool.exe
OpenTurbine_Recommended.zip
OpenTurbine_Recommended.zip.sha256
```
