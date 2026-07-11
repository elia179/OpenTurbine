# OpenTurbine Setup Tool

Windows setup/update helper for OpenTurbine boards.

The home screen separates **Clean install / reinstall** (USB, erases the entire
selected board) from **Update and keep my setup** (Wi-Fi, backs up and retains
the existing engine setup). Do not weaken this distinction in release builds.

The CP210x button downloads the complete signed Silicon Labs CP210x Universal
Windows Driver v11.5.0 from Silicon Labs, verifies its pinned SHA-256, and asks
Windows `pnputil` to install the signed INF/CAT package. The EXE does not embed
or redistribute proprietary driver binaries.

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
  --cp210x-driver C:\path\to\extracted\CP210x_Windows_Drivers `
  --ch340-driver C:\path\to\extracted\CH341SER
```

Release packages require both complete driver packages. Keep each full extracted
vendor folder; a copied installer alone is not sufficient. The setup tool can
also download the pinned Silicon Labs CP210x Universal driver directly and
install its signed INF/CAT package with `pnputil`. CH340/CH341/CH343 fallback
installation uses the WCH installer bundled in the recommended ZIP.

For local packaging validation only, `--allow-missing-drivers` may be used. Do
not publish that development package as the recommended release.

Release assets:

```text
OpenTurbineSetupTool.exe
OpenTurbineSetupTool.exe.sha256
OpenTurbine_Recommended.zip
OpenTurbine_Recommended.zip.sha256
```
