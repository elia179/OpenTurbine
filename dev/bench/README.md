# OpenTurbine bench rig (hardware-in-the-loop)

Two ESP32s on the table, wired pin-to-pin, so OpenTurbine can be exercised end to
end with no turbine attached:

- **DUT** — an ESP32-S3 running OpenTurbine (the firmware under test).
- **Tester** — a classic ESP32 running `firmware/` (OTBench), a dumb I/O slave.
- **Brain** — the PC. It drives the tester over USB serial and the DUT over its
  Wi-Fi web API, closing the loop: inject a stimulus on one side, assert the
  other side agrees.

```
   PC ──USB serial──> OTBench tester ──wires──> OpenTurbine S3 <──Wi-Fi── PC
        (drive/read)                  (pin↔pin)              (192.168.4.1)
```

`pinmap.json` is the single source of truth for the wiring and is shared by both
the tester firmware and the PC harness.

---

## 1. Wiring

`DUT GPIO` are OpenTurbine pins (set them on the S3 Hardware page); `Tester GPIO`
are fixed in the OTBench firmware. **Every link gets a 470 Ω–1 kΩ series
resistor**, and the two boards **must share ground**.

### Tester drives → DUT inputs

| Signal      | DUT S3 | Tester | Type            | Notes |
|-------------|-------:|-------:|-----------------|-------|
| START       | 13     | 13     | digital act-low | LOW = press, Hi-Z = release |
| STOP        | 15     | 14     | digital act-low | |
| N1 (RPM)    | 14     | 4      | pulse           | square wave, freq = rpm·ppr/60 |
| THROTTLE_IN | 4      | 25     | analog (DAC)    | true DAC |
| OILP        | 1      | 26     | analog (DAC)    | true DAC |
| FLAME       | 2      | 27     | digital         | HIGH/LOW crosses the flame threshold |
| IDLE_IN     | 5      | 32     | digital · opt   | extremes only (no 3rd DAC); or tie S3 G5 to GND |

### DUT outputs → tester reads

| Signal       | DUT S3 | Tester | Type      | Notes |
|--------------|-------:|-------:|-----------|-------|
| THROTTLE_OUT | 16     | 18     | servo PWM | 1000–2000 µs @ 50 Hz |
| STARTER_OUT  | 17     | 19     | servo PWM | only if OT_HAS_STARTER |
| OILPUMP_OUT  | 11     | 21     | LEDC PWM  | ~10 kHz, duty ≈ oil % |
| FUEL_SOL     | 12     | 22     | digital   | |
| IGNITER      | 21     | 23     | digital   | LEDC capture if using PWM dwell |
| STARTER_EN   | 39     | 33     | digital   | |

Plus **GND ↔ GND**. 13 signal jumpers + 1 ground.

Only two clean analog channels are available — the classic ESP32's true DACs
(GPIO 25/26), spent on THROTTLE_IN and OILP. With no smoothing caps, FLAME (a
threshold sensor) and IDLE_IN are driven as plain digital HIGH/LOW rather than a
swept voltage. The tester avoids input-only pins (34–39, and G36/G39 aren't
broken out on this board), GPIO 16/17 (PSRAM on WROVER modules) and the strapping
pins (0/2/5/12/15) so the DUT holding a line at power-on can't stop the tester
from flashing.

---

## 2. Set up the DUT (ESP32-S3)

On the OpenTurbine **Hardware** web page, set pins to match the `DUT GPIO`
column and enable: N1 RPM, oil pressure, flame, throttle input (ADC), idle input
(ADC), throttle ESC, oil pump, fuel solenoid, igniter. `starter_en` and starter
ESC are optional (their tests skip if absent). `verify-wiring` (below) checks the
live config against the map.

The suite toggles **Dev Mode** and **Bench Mode** itself for the sequence test;
you don't need to pre-set them.

## 3. Flash the tester (classic ESP32)

```
cd bench/firmware
pio run -t upload            # add --upload-port COMx if needed
```

## 4. Run it (from the PC)

Wired Ethernet for internet, Wi-Fi joined to the S3 AP (`192.168.4.1`). The
tester is on its own COM port — pass it explicitly since several serial devices
are usually present.

```
cd bench/harness
pip install -r requirements.txt

python run.py --port COM6 doctor            # connectivity both sides
python run.py --port COM6 verify-wiring     # DUT config vs pin map
python run.py --port COM6 monitor --secs 20 # live telemetry + pin reads
python run.py --port COM6 run               # basic suite
python run.py --port COM6 run --advanced -v # + start-switch & bench sequence
python run.py --port COM6 run --json out.json
```

Ad-hoc probing:

```
python run.py --port COM6 tester GET IGNITER
python run.py --port COM6 tester SET N1 83
python run.py --port COM6 dut-cmd IGN_TEST
python run.py --port COM6 dut-data mode
```

Set `OTBENCH_PORT=COM6` to skip `--port`.

---

## 5. What the suite checks

- **Input paths:** RPM readback, throttle-input voltage sweep, oil-pressure
  voltage, flame threshold, STOP switch → assert DUT telemetry matches.
- **Output paths:** fire the STANDBY actuator self-tests (IGN_TEST, OIL_PRIME,
  FUEL_SOL_TEST, STARTER_EN_TEST) → assert the tester measures the pin drive.
- **Advanced:** START switch, and a bench-mode timed startup that confirms the
  oil pump and igniter actually fire on their sequence pins.

## 6. Serial protocol (PC ↔ tester)

Newline-terminated ASCII, 115200 baud, one reply line per command:

```
PING                 -> OK OTBench <ver>
LIST                 -> SIG <name> <kind> gpio=<n> ...  then OK
RESET                -> OK                 (all driven outputs to safe/idle)
SET <name> <value>   -> OK | ERR ...       digital: 1/0 · freq: Hz · analog: volts
GET <name>           -> VAL <name> level=.. | us=.. hz=.. duty=.. level=..
STATE                -> VAL STATE <name>=.. ...   (all inputs in one shot)
```

## 7. Limits / next steps

- **SPI thermocouples (TOT/TIT/oil-temp)** aren't driven yet — emulating a
  MAX6675/31855 as an SPI slave is a future OTBench module. For now, test
  temperature-driven logic (overtemp, EGT flameout) via Dev-Mode value injection
  once that endpoint exists, or add the SPI-slave emulator.
- **Only 2 clean analog (DAC) channels** on the tester (GPIO 25/26 → THROTTLE_IN,
  OILP). Without smoothing caps, FLAME and IDLE_IN are digital HIGH/LOW, not swept.
  Add an MCP4728 (4-ch I²C DAC) if you need more true-analog channels.
- **NeoPixel status LED** (WS2812) isn't decoded — use a plain-GPIO status LED on
  the bench profile if you want to assert LED state.
- **Safety reactions** (overspeed/overtemp → shutdown within N ms) are the
  highest-value tests to add next; they build directly on the input-injection
  path already here.
