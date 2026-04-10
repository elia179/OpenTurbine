# GPX750 Cluster — OpenTurbine example

Kawasaki GPX750 R 1988 instrument cluster firmware adapted for the
**OpenTurbine ClusterSerial protocol v2**.

Originally based on `JetEcu/GPX750_Cluster`.  The ECU communication layer and
configuration system have been replaced; all display, gauge, and indicator
logic is unchanged.

---

## What changed from JetEcu/GPX750_Cluster

| File | Change |
|---|---|
| `src/OTComm.h` | **New** — replaces `EcuComm.h`. Parses OT protocol v2 (schema-first, positional data) |
| `src/WifiConfig.h` | **New** — replaces `BtConfig.h`. Wi-Fi AP + web config page + OTA |
| `src/config_html.h` | **New** — config page HTML baked into flash (no `uploadfs` needed) |
| `src/KicConfig.h` | ECU baud → 460800, BT constants removed, Wi-Fi AP constants added |
| `src/main.cpp` | Uses `OTComm`/`WifiConfig`; `#if DEMO_MODE`/`BOOT_SWEEP` → runtime flags |
| `src/BtConfig.h` | **Deleted** — replaced by `WifiConfig.h` |
| `platformio.ini` | Added ESPAsyncWebServer, ArduinoJson, LittleFS; removed BT library |

---

## Configuration — web UI

1. Power on the cluster
2. Connect a phone or laptop to Wi-Fi **`GPX750-Cluster`** (open, no password)
3. Open **`http://192.168.4.1`** or **`http://cluster.local`** (mDNS, works on Mac / Linux / Windows 10+)
4. Adjust settings and press **Save** — most changes take effect immediately; demo mode and boot sweep apply on the next reboot

Config is stored at `/cluster_cfg.json` on LittleFS.  Factory defaults are in `KicConfig.h`.

### What's configurable

| Section | Fields |
|---|---|
| Display | RPM header mode, demo mode, boot gauge sweep |
| Engine limits | N1 max, N1 warn, N2 warn, TOT max, TOT warn, oil warn, fuel warn % |
| Signal loss | Enter / exit NO SIGNAL hysteresis (ms) |
| Tachometer | Gauge full-scale Hz |
| Temp gauge PWM | 5-point piecewise-linear cal (0 / 25 / 50 / 75 / 100 %) |
| Fuel gauge PWM | 5-point piecewise-linear cal |
| Fuel sender ADC | Empty / full raw ADC counts (0–4095) |

> **Engine limit note:** The cluster seeds its limits from `KicConfig.h` and the saved config.  When the ECU connects it sends an `L:` line that overrides all limits for the current session — so you only need to adjust the cluster limits if the ECU is unavailable (demo mode, bench testing).

### OTA firmware update

On the config page, scroll to **Firmware update**, select the `.bin` from `.pio/build/esp32dev/firmware.bin`, and press **Flash**.  The cluster reboots automatically when done.

---

## Hardware wiring

| Signal | ECU side | Cluster side |
|---|---|---|
| Cluster data | `OT_CLUSTER_TX_PIN` (GPIO in `hardware_profile.h`) | `ECU_RX_PIN` (GPIO 16 default) |
| Ground | GND | GND |

TX-only protocol — no wire needed from cluster TX to ECU.

**Baud rate:** 460800, 8N1.  Set `OT_CLUSTER_BAUD 460800UL` in your OpenTurbine `hardware_profile.h` (or leave it out — 460800 is the new default in `ClusterSerial.h`).

---

## Build

```bash
cd examples/GPX750_Cluster
pio run -e esp32dev --target upload
```

Adjust pin assignments and factory defaults in `src/KicConfig.h` before flashing.
AP SSID and password are also set there.
