# OpenTurbine — Real Issues Found During HIL Validation

Running log of **genuine problems** (firmware bugs, footguns, tooling gotchas) found on
the bench rig, so they can be re-checked later. Harness/test-expectation artifacts that
turned out NOT to be firmware defects are noted at the bottom for context, not as bugs.

Status legend: 🔴 open · 🟢 fixed (this session) · 🟡 workaround / needs follow-up

Started 2026-07-07 (branch `bench-hil-validation`, fw 1.4.0). All firmware fixes below have
been flashed and validated on the S3 (a mid-session flash left the AP unreachable until a
power-cycle; see gotcha #4 for the root cause — a Windows platformio Unicode crash).

---

## Firmware

### 🟢 1. ServoActuator dead on ESP32-S3 (throttle/starter ESC emit nothing)
`ServoActuator::begin()` hardcoded `ledcAttach(pin, 50, 16)` (50 Hz @ 16-bit). The S3's
LEDC clock can't satisfy that combo, so the attach FAILS and the servo/ESC pin emits
nothing — on an S3 the throttle ESC + starter servo produce no signal at all. Classic
ESP32 is fine (80 MHz APB attaches 16-bit@50Hz). `LEDCActuator` already had the
attach-retry fix; `ServoActuator` never got it.
**Fix:** attach-retry 16→12-bit floor with runtime `_maxDuty` tracking
(`src/hal/actuators/ServoActuator.h`). Verified: throttle now ramps on the bench.

### 🟢 2. OilPrime silently fails to prime when the oil control loop is disabled
With an oil pressure sensor, `OilPrime` sets a pressure *target* (`ed.oilTargetBar`) and
delegates actual pump drive to the oil control loop (`g_ctrlOilLoop.tick()`). If the oil
loop controller is OFF (it's user-toggleable), nothing drives the pump → pressure never
builds → OilPrime times out into an abort, with no explanation. In bench mode the block
even prints "Oil pump 100%" while the pump is actually at 0.
**Fix (final):** `OilPrime` now **drives the pump directly at `startupOilPct`** whenever the
oil loop isn't regulating (loop disabled, or bench mode) instead of only setting a target the
loop would act on. So it builds pressure and completes even with the loop off. It also now
commands the pump every tick during the prime, fixing the bench-mode case that printed
"Oil pump 100%" while the pump was actually at 0. The earlier preflight warning was removed
as it's no longer true. (`src/engine/sequencer/blocks/OilPrime.h`.)

### 🟡 3. Standby oil feed was fixed-% only — added set-pressure option (needs HW validation)
Standby oil feed commanded a fixed pump % only. Added a pressure-target mode:
`standby_oil.feed_bar` (Config `standbyOilFeedBar`, telemetry `standby_oil_feed_bar`,
config UI "Feed Pressure (bar)"). When > 0 with an oil sensor + the oil control loop
enabled, the standby feed regulates the pump to that pressure via the tuned `g_ctrlOilLoop`
(reused; dt-normalised + failsafe), floored at Feed Duty %. Default 0 = fixed-% (unchanged).
Disengage path clears the target + pump so pressure mode can't leave the pump running in
standby. **Compiles clean. NOT yet validated on hardware** (DUT was down — see note). The
oil loop no-ops in bench mode, so validate on a real (non-bench) start: enable oil loop,
set feed_bar, drive a shaft above windmill RPM, confirm the pump holds the target pressure
and drops to the operator value when windmilling stops. (task #29)

### 🟢 6. LOW_OIL protection silently disarmed by a sequence with no oil-arming block
`LOW_OIL` only trips once a startup block sets `oilMinBar > 0` — done by OilPrime (on
completion), StarterSpin, Spool, or SafetyHold. A hand-built startup sequence omitting all
four leaves low-oil protection off even after reaching RUNNING, with no indication. (This
was session-1 engineering note #2.)
**Fix:** preflight `seq_issue` warning when `safetyLowOil && hasOilPress` but no startup
block arms the oil minimum (`src/main.cpp` sequence validation). Compiles; pending HW check.

### 🟢 7. Afterburner ignition faulted on default settings — now falls back to the fitted igniter
`ABIgnite` has two ignition methods: torch (fuel spike, silently skipped unless
`torch_tot_limit > 0`) and the AB igniter (igniter2, `use_igniter` defaults false). So default
AB settings had NO active method → runtime fault. Preflight already warned
(checkAbIgnitionBlock), but it still failed to light out of the box.
**Fix:** `ABIgnite` now **falls back to firing the fitted AB igniter (igniter2) when no method
is explicitly configured** — a spark needs no EGT cap, so default AB lights whenever the
ignition hardware exists. If no igniter2 is fitted either, it still faults (correct) and the
preflight warning stands. (`src/engine/sequencer/blocks/ABIgnite.h`.)

### 🟢 8. Captive portal served the full dashboard → poor captive UX + starved the hardware page
The OS captive-portal probes (`/generate_204`, `/hotspot-detect.html`, …) were served the
FULL dashboard (`index.html`). The dashboard opens a WebSocket, and the captive mini-browser
(CNA/WebView) showing it then held the single `/ws` slot (`cleanupClients(1)` keeps only 1,
by design — multiple clients cause `canSend()` frame drops). Result: (a) a heavy/janky
captive experience, and (b) the real browser's **hardware page couldn't get the WS** →
fell back to the 3 s status poll until the captive webview closed or the user re-navigated
(then the WS slot freed → fast 1 s updates). This is exactly the user-reported "captive
redirect doesn't work" + "hardware page only updates every ~3s until I switch pages" pair.
**Fix (two parts):** (1) a small self-contained landing page at `/portal` (link to the panel,
no app.js, no WebSocket) — frees the `/ws` slot so the hardware page connects at 1 s. (2) The
OS probes (`/generate_204`, `/connecttest.txt`, `/ncsi.txt`, `/redirect`, `/hotspot-detect.html`,
…) and the captive catch-all now return **302 → `Location: http://<ap-ip>/portal`** instead
of a bare 200 page. A 200 left Windows unable to learn the portal URL, so Edge opened its
own default (msn.com over the PC's wired internet). The 302+Location gives Windows the portal
URL explicitly. Verified on the bench: probes return 302→/portal, /portal is 732 B, `/` still
serves the full app. **NOTE:** on a PC that is BOTH on the AP and on wired internet, Windows/
Edge captive behavior can still escape to msn via the wired route — the reliable target is a
Wi-Fi-only device (phone). `192.168.4.1` / `ot.local` always work directly.

---

## Tooling / environment

### 🟡 4. platformio flash crashes on Windows with UnicodeEncodeError (looks like a hang)
`pio run -t upload` prints a Unicode progress bar (`█`); on Windows the default cp1252
console encoding can't encode it, so platformio throws `UnicodeEncodeError` mid-upload
and exits 255 — the upload appears to "hang"/fail even though esptool is fine.
**Workaround:** set `$env:PYTHONIOENCODING='utf-8'; $env:PYTHONUTF8='1'` before invoking
platformio. With that, the flash completes cleanly. Also: pass `--upload-port COM4`
explicitly (auto-detect can pick COM3, the tester).

### 🟢 5. Bench harness aborted whole run on a truncated /api/data read
WiFi can truncate a large `/api/data` body mid-stream → invalid JSON → `dut._get` raised
and killed the entire test (seen once in `relight_test`).
**Fix:** `dut._get` now retries up to 3× on `JSONDecodeError` (`bench/harness/otbench/dut.py`).

---

### 🟡 9. Bench STARTER_OUT path (DUT GPIO17 ↔ tester GPIO19) is physically dead — NOT firmware
A ServoActuator on DUT GPIO17 read us=0 at the tester. Root-caused properly: a **digital**
output on DUT GPIO17 ALSO reads level=0 at tester STARTER_OUT (both on and off). So it isn't
an LEDC/servo quirk — the whole DUT-GPIO17→tester-GPIO19 jumper/capture path is non-functional
(never actually exercised before, since the starter servo was only ever enabled as starter_en
on the bench). **This is a bench wiring/pin fault, NOT a firmware or servo-driver defect** —
servo output is validated on GPIO40 (governor-driven prop-pitch 999→1364 µs) and digital output
on GPIO39/12/21/11. **Action for the user:** re-seat/replace the DUT-GPIO17↔tester-GPIO19
jumper (or check tester GPIO19); it's purely a bench rig issue. (Same symptom class as the old
GPIO18 path that forced THROTTLE_OUT 18→40 — likely the same flaky jumper region.)

## Optional hardware simulated via pin-remap (2026-07-07) — all validated
- **Sensors (9):** P1, P2, fuel-press, torque, battery (ADC via the throttle DAC pin), oil-temp
  NTC (inverse response), TIT (thermocouple emulator, reaches 650 °C), fuel-flow (pulse via
  FREQ_OUT), and oil-pump current + overcurrent trip. All read + respond + healthy.
- **Actuators (7):** cool fan, bleed valve, airstarter, oil scavenge (relay/digital), glow plug
  (LEDC PWM), fuel pump 2 (servo), prop pitch (servo, governor-driven on GPIO40). All drive
  their outputs. (Digital valves default active-LOW — set active_h per your wiring.)
- Scripts: `optional_sensors_test.py`, `optional_actuators_test.py`.

## Tester firmware upgrades (2026-07-07)
- **SERVO_OUT signal kind added** to OTBench — generates a 50 Hz RC/servo pulse (`SET <name>
  <microseconds>`). IDLE_IN (tester GPIO32 → DUT GPIO5) repurposed from digital to SERVO_OUT.
  **Validated RC-PWM idle input:** 1000/1500/2000 µs → rc_idle_norm 0 / 0.49 / 1.0, valid
  throughout (throttle RC uses the identical pulseIn path). Unblocks RC-PWM input testing.
- **Independent N1/N2 — FIXED.** The Arduino `ledcAttach*`/`ledcWriteTone`/`ledcChangeFrequency`
  wrappers kept reusing one LEDC timer (N1 clobbered N2). Rewrote the tester's FREQ_OUT/SERVO_OUT
  handling on the **raw ESP-IDF LEDC driver** (`ledc_timer_config`/`ledc_channel_config` with a
  distinct `timer_num`==`channel` per signal, `ledc_set_freq` per timer). Verified independent:
  drive (45k/34k), (30k/52k), (60k/20k) → both read correctly. This unblocked the **direct
  N1-max pullback under the governor** test: N1 65k (band 58-70k) with N2 held low → throttle
  1.000 → 0.080 (previously provable only via EGT). Files: `bench/firmware/src/main.cpp`.

## Cross-platform: OpenTurbine on the classic ESP32 (2026-07-07)
Built OpenTurbine for `env:esp32dev` (all this session's changes compile for classic ESP32,
94.5% flash) and flashed it onto the classic board (the tester) for a standalone boot test via
serial. It comes up fully healthy: LittleFS OK, config self-completes, `[VALIDATE] All
sequences OK`, WiFi AP + web server up. Key platform check — the ServoActuator attaches at the
FULL resolution on classic: `[THROTTLE_SRV] servo attach pin=22 freq=50Hz bits=16 OK`. This
confirms the attach-retry fix is platform-correct: **16-bit on the classic ESP32**, and it only
falls back to 12-bit on the S3 (where 16-bit@50Hz fails). No regression on the classic platform.
OTBench was reflashed afterwards; the HIL bench is back to normal.
- Note: a full HIL role-swap (classic = DUT, S3 = tester) is NOT cleanly feasible with this
  wiring — the S3 has no DAC (can't drive true analog as a tester) and the classic's DAC-wired
  pins are ADC2 (conflicts with WiFi when it's the DUT). Digital/freq/output-only swap is
  possible but needs OTBench built for the S3 + a classic-pin OpenTurbine profile.

## Role-reversed HIL: OpenTurbine on the classic ESP32, S3 as tester (2026-07-07)
Built a second OTBench env `otbench_s3` (`-DOTBENCH_S3`, board esp32-s3-devkitc-1) with an
S3-side signal map (S3 GPIOs = pinmap dut_gpio, directions flipped; MAX6675 emulator + DAC
guarded out — S3 has no DAC; servo LEDC clamped to 14-bit on S3). Flashed OTBench→S3 (COM4)
and OpenTurbine→classic (COM3, esp32dev). **Read the classic ESP32's outputs on the S3
tester, 4/4:** oil_pump (LEDC, duty 1.00), fuel_sol, igniter, starter_en (digital). Drives
via OpenTurbine's Tools self-tests over WiFi (classic AP; API works without the filesystem).
- **Throttle servo output not readable:** its jumper lands on classic GPIO17, which is a
  WROVER **PSRAM** pin on this classic module — OpenTurbine's config validation correctly
  refuses to output there ("Invalid hardware section JSON"). A wiring/module constraint, not
  firmware. (Same reason the original pinmap avoided GPIO16/17.)
- To restore the normal bench: flash OpenTurbine→S3 (`esp32s3dev`, +uploadfs +restore_bench)
  and OTBench→classic (`otbench`). Scripts: `role_reversed_outputs_test.py`.

### Classic ESP32 pin-function sign-off (verified on both chips)
Every pin FUNCTION reachable with this wiring validated on the classic ESP32 (DUT), read/driven
by the S3 tester:
- **SERVO output** — throttle servo on GPIO21 → S3 read a clean **1599 µs** pulse @60%. Full
  16-bit LEDC servo works on the classic (the S3 needs 12-bit; classic keeps 16-bit).
- **LEDC PWM output** — oil_pump GPIO21 → duty 1.00.
- **DIGITAL outputs** — fuel_sol/igniter/starter_en (GPIO22/23/33) → all read active.
- **FREQ input** (PCNT) — drive 45000 → classic reads 44705 on GPIO4.
- **ADC input** — GPIO32 (ADC1) reads full range **0 ↔ 4095**.
- **DIGITAL input** — DI channel on GPIO27 tracks high/low.
Gaps (need a different rig, NOT firmware bugs): thermocouple SPI (classic input-only pins 34/35
can't be an SPI master on the TOT jumpers), a full analog *sweep* (S3 has no DAC — extremes
only, which still proves the ADC), and ADC2 pins (unusable for ADC with WiFi — a known classic
constraint). Config note: the `controls` (start/stop pin) section must set both pins coherently
or a hardware POST is rejected as "Invalid hardware section JSON" (config-structure, not a pin
issue). Scripts: `classic_pinfunc_test.py`.

## Static code reviews (no hardware — DUT was down)

- **Digital-input polling** (`checkGeneralDI`, main.cpp): reviewed. Debounced per-channel;
  fault/estop roles are level-sensitive and gated to STARTUP/RUNNING; ab_arm resolved from
  level + dedicated switch. No defects found.
- **Afterburner state machine** (`checkABTrigger` + enter/abort/fault/done, main.cpp):
  reviewed. Rising-edge latch prevents re-entry loops from a held trigger; Fault≠Off
  distinction is deliberate; manual vs auto-trigger correctly separated (manual AB isn't
  shut by trigger release); custom ignition seq without ABStabilize is force-promoted to
  Running so trigger-release can still stop it; AB shuts down if the engine leaves RUNNING.
  No defects found.
- **Rules engine** (`RulesEngine::evaluate` + eval/apply, RulesEngine.h): reviewed.
  STARTUP/RUNNING-only + bench-disabled; hysteresis latches reset on reload; sensor/actuator
  usability gated; negative onValue/offValue = "leave unchanged"; mid-loop `return` on
  SHUTDOWN stops re-asserting actuators after a cut — and `REQUEST_FAULT` routes through
  `enterFaultShutdown()` which sets mode=SHUTDOWN (main.cpp:1796), so that same guard also
  covers rule-triggered faults. No defects found.

## Pending hardware validation (need the DUT powered) + ready-to-run scripts

- `campaign/standby_oil_pressure_test.py` — validates issue #3 (standby-oil pressure mode).
- `campaign/calibration_pipeline_test.py` — oil cubic scaling, flame threshold, fuel-pump-min
  save, oil zero-reachability (task #25).
- **Digital-input channels + AB I/O** (tasks #26/#28): the bench wires cover START/STOP
  switches (already in the basic suite) but NOT the 4 general DI channels, AB-arm, AB flame,
  or AB sol/pump/igniter outputs. To test those, remap the DI/AB functions onto wired tester
  channels (e.g. an AB actuator onto a spare wired output pin, AB flame onto the FLAME ADC,
  AB-arm/DI onto the STOP input) — same pin-reuse approach the governor N2 test used. Author
  once the DUT is back so the scripts can be debugged live.

## NOT bugs (harness / stale-test artifacts, recorded so we don't re-chase them)

- **OIL_ZERO "FAIL":** `only_safety()`'s strict per-key `is` verify hit the hardware-POST
  reboot race and reported the arm as failed. Direct test: OIL_ZERO trips in 0.08 s when
  armed. Safety-block round-trip persists correctly. Not a firmware defect.
- **Governor / fuel-floor "FAIL"s:** tests asserted the OLD 8% idle fallback. By design the
  running floor is now `fuel_pump_min_pct` only (0 = no floor). Firmware is correct; test
  expectations were updated (`governor_test.py`, `fuelpump_min_test.py`).
- **config_test out-of-range "FAIL":** firmware REJECTS an invalid `rpm_limit` at PATCH
  (400 "settings rejected") rather than accept-and-warn — better than the test expected.
  Test updated to assert rejection.
- **seq_test "OilPrime drove oil pump" FAIL:** root cause was the oil loop being disabled
  (issue #2 above), plus harness aliasing when oil was already at target. Test now enables
  the oil loop and primes with low oil.
