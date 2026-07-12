# Windows USB Driver Acceptance

Use this checklist on a Windows 10/11 PC or VM that does not already have the
target USB-serial driver installed. Save the generated diagnostics ZIP/logs from
each run.

## CP210x

1. Connect a Silicon Labs CP210x board (`VID_10C4`).
2. Start the same-release `OpenTurbineSetupTool.exe` and
   `OpenTurbine_Recommended.zip`.
3. Confirm the driver screen offers only **Install CP210x**.
4. Accept UAC.
5. Confirm the tool reports the matching `COMx` port and continues without an
   app restart.
6. Complete board detection/flash.
7. Save `%LOCALAPPDATA%\OpenTurbine\SetupTool\logs\driver-install-*.json` and
   `.txt`.

## WCH CH340/CH341/CH343

1. Connect a WCH board (`VID_1A86` or `VID_1A2C`).
2. Start the same-release `OpenTurbineSetupTool.exe` and
   `OpenTurbine_Recommended.zip`.
3. Confirm the driver screen offers only **Install WCH**.
4. Accept UAC.
5. Confirm the tool reports the matching `COMx` port and continues without an
   app restart.
6. Complete board detection/flash.
7. Save `%LOCALAPPDATA%\OpenTurbine\SetupTool\logs\driver-install-*.json` and
   `.txt`.

## Edge Cases

- Cancel UAC and confirm the tool reports user cancellation.
- Run as a standard user and enter separate admin credentials at UAC.
- Test with multiple unrelated COM devices connected.
- Disconnect the board during installation and verify the diagnostic log names
  the missing matching COM port.
- Test on a non-English Windows installation.
- If the setup tool asks for a Windows restart, confirm the result JSON has
  `reboot_required: true`; the tool must not restart Windows automatically.

For a read-only support bundle, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\setup_tool\collect_driver_diagnostics.ps1
```
