# Comms-protocols subsystem audit

## Files reviewed

| File | Lines read |
|---|---|
| `src/system/ClusterSerial.h` | 1-76 (full) |
| `src/system/ClusterSerial.cpp` | 1-189 (full) |
| `src/system/MAVLinkOutput.h` | 1-188 (full) |
| `src/system/CommandQueue.h` | 1-84 (full) |
| `src/system/CommandQueue.cpp` | 1-3 (full) |
| `src/main.cpp` | 610-626, 1157, 1299-1333, 1428-1470, 1596-1680 |
| `src/system/web/WebServer.cpp` | 538-709 (command dispatch) |
| `src/system/HardwareConfig.h` | 27-30, 137-142, 271-280 |
| `src/system/HardwareConfig.cpp` | 111, 149, 265-267, 445-542, 1154-1167 |
| `src/system/Watchdog.h` | 1-24 (full) |

---

## Findings (Critical -> Info)

---

### ECU-COM-01: mavlinkIntervalMs=0 removes rate gate, flooding UART and stalling main loop

- **Severity:** Critical
- **Bug class:** Missing zero-guard / blocking UART write
- **Location:** `src/system/MAVLinkOutput.h:51`, `src/system/HardwareConfig.cpp:1167`
- **Description:** `MAVLinkOutput::tick()` uses a single guard:
  ```cpp
  if ((now - _lastMs) < (unsigned long)hw.mavlinkIntervalMs) return;
  ```
  When `mavlinkIntervalMs` is 0 the cast produces `(unsigned long)0`. The left-hand side (`now - _lastMs`) is always `>= 0`, so the condition is never true and the guard never fires. Every call to `tick()` -- once per main loop iteration -- sends a full burst of MAVLink packets.
- **Trigger:** An operator (or corrupted config) sets `interval_ms: 0` in the `mavlink` config block. The ArduinoJSON expression `mvl["interval_ms"] | mavlinkIntervalMs` at HardwareConfig.cpp:1167 accepts and stores the value 0, overriding the 100 ms default. There is no floor clamp after loading.
- **Impact:** With all nine optional sensor channels healthy (`hasFuelFlow` etc.) the burst is 17 (HEARTBEAT) + 11x26 (NAMED_VALUE_FLOAT) = 303 bytes per loop tick. At 57600 baud the ESP32 HardwareSerial TX ring buffer (256 bytes by default) overflows on the first burst. `HardwareSerial::write()` is a blocking call; it busy-waits until the UART hardware clocks out the overflow bytes. At 57600 baud 47 bytes of overflow takes ~8 ms; a fast loop running at kHz rates will pile up and stall the main loop indefinitely, triggering the 5-second TWDT reset and hard-restarting the ECU while the engine is running.
- **Evidence:**
  - `MAVLinkOutput.h:51`: no lower-bound check on `hw.mavlinkIntervalMs`.
  - `HardwareConfig.cpp:1167`: raw JSON value assigned without clamping.
  - `ClusterSerial.cpp:168`: by contrast ClusterSerial has `if (interval == 0) interval = 50;` -- the same pattern is absent in MAVLink.
- **Suggested fix:** Add a floor clamp immediately after loading or inside `tick()`:
  ```cpp
  unsigned long iv = (unsigned long)hw.mavlinkIntervalMs;
  if (iv == 0) iv = 100;   // minimum 100 ms floor
  if ((now - _lastMs) < iv) return;
  ```
  Alternatively clamp during config load: `if (mavlinkIntervalMs < 20) mavlinkIntervalMs = 100;`
- **Confidence:** High

---

### ECU-COM-02: Web STOP and AB_STOP silently dropped when CommandQueue is full; HTTP 200 returned

- **Severity:** High
- **Bug class:** Silent drop of safety-critical command / misleading API response
- **Location:** `src/system/web/WebServer.cpp:649,707`, `src/system/CommandQueue.h:68-71`
- **Description:** `CommandQueue::push()` is documented as non-blocking and returns `false` when the 16-slot FreeRTOS queue is full. The web server never inspects the return value; every command endpoint responds with `{"ok":true}` regardless:
  ```cpp
  CommandQueue::push({ OTCommand::STOP });
  req->send(200, "application/json", "{\"ok\":true}");
  ```
  Under burst conditions (operator or captive-portal page fires several test commands in rapid succession) the queue fills to depth 16. A subsequent `OTCommand::STOP` or `OTCommand::AB_STOP` from the web UI is silently discarded while the operator's browser displays a success response.
- **Trigger:** Realistic scenario: operator opens the test tools panel and clicks FUEL_PRIME, OIL_PRIME, IGN_TEST, IGN2_TEST, AB_SOL_TEST, AB_PUMP_TEST, COOL_FAN_TEST, OIL_SCAV_TEST, AIRSTARTER_TEST, BLEED_VALVE_TEST, GLOW_TEST (11 commands), then FUEL_SOL_TEST, START_TEST, IDLE_TEST, SET_OIL_DEMAND, SET_OIL_PCT (5 more) before the ECU drains a single tick -- 16 entries consumed. The next STOP push returns false and the push is lost. The physical STOP switch still functions (it calls `enterShutdown()` directly, bypassing the queue), but the web UI STOP button is unreliable under queue saturation.
- **Impact:** The operator sees HTTP 200 OK but the engine continues running after pressing STOP from the web UI. AB_STOP is equally affected -- the afterburner cannot be shut down via web while the queue is saturated.
- **Evidence:**
  - `WebServer.cpp:643-650`: push return value discarded.
  - `WebServer.cpp:707-708`: generic command dispatch also discards return value.
  - `CommandQueue.h:68-71`: `push()` returns `bool`, explicitly documented as drop-on-overflow.
- **Suggested fix:**
  1. Check the return value and return HTTP 503 (or a JSON error body) when push fails so the operator knows the command was not accepted.
  2. Consider priority-queuing safety commands (STOP, AB_STOP, AB_FIRE) by checking `xQueueSendToFront()` so they jump the queue, or drain actuator-test commands before the safety command arrives.
  3. Reduce the surface area: return 503 immediately and disable the button in the UI to prevent re-submission.
- **Confidence:** High

---

### ECU-COM-03: MAVLink 303-byte burst exceeds 256-byte HardwareSerial TX buffer, causing main-loop blocking write

- **Severity:** High
- **Bug class:** Blocking UART write / jitter in real-time loop
- **Location:** `src/system/MAVLinkOutput.h:165-178` (`_sendEngineData`), `_sendPacket`
- **Description:** When all optional sensor channels are healthy (n2Healthy, oilTempHealthy, battHealthy, fuelPressHealthy, torqueHealthy, titHealthy, hasFuelFlow all true) `_sendEngineData()` sends 11 NAMED_VALUE_FLOAT frames plus 1 HEARTBEAT per tick:
  ```
  HEARTBEAT:        6 + 9 + 2 = 17 bytes
  11x NAMED_VALUE_FLOAT: 11 * (6 + 18 + 2) = 286 bytes
  Total: 303 bytes per tick
  ```
  The ESP32 Arduino HardwareSerial TX ring buffer defaults to 256 bytes. `HardwareSerial::write()` blocks the caller until ring-buffer space is available; it does not return early or drop bytes. Attempting to write 303 bytes atomically overflows the buffer by 47 bytes. At 57600 baud (the HardwareConfig default) 47 bytes takes approximately 8 ms to drain, during which the Core 1 loop is blocked.
- **Trigger:** Active during every MAVLink tick when all sensor channels are healthy and `mavlinkBaud` is at the default 57600. The block repeats every `mavlinkIntervalMs` (default 100 ms), injecting ~8 ms of latency into every main-loop cycle that contains a MAVLink tick.
- **Impact:** 8 ms stall every 100 ms is well within the 5-second WDT budget and will not trigger a reset under the default configuration. However it is a deterministic priority inversion: safety checks, sensor reads, and actuator writes all stall for the duration. At higher baud rates (115200) the block shrinks to ~4 ms; at 460800 it disappears. If ECU-COM-01 (zero interval) is also present the blocking becomes continuous.
- **Evidence:**
  - `MAVLinkOutput.h:165-178`: up to 12 `_sendPacket()` calls per tick.
  - `MAVLinkOutput.h:124-127`: each `_sendPacket()` makes 4 `write()` calls with no `availableForWrite()` check.
  - `HardwareConfig.cpp:457`: default `mavlinkBaud = 57600`.
  - `HardwareConfig.cpp:149`: default `mavlinkIntervalMs = 100`.
- **Suggested fix:** Check `availableForWrite()` before each packet and skip or defer when headroom is insufficient:
  ```cpp
  void _sendPacket(...) {
      if (_serial->availableForWrite() < (6 + payLen + 2)) return; // drop packet rather than block
      ...
  }
  ```
  Alternatively raise `mavlinkBaud` to 115200 or 460800 in the default config, and document the minimum baud relative to sensor count. Increasing the HardwareSerial TX buffer via `_serial->setTxBufferSize(512)` in `begin()` also eliminates the stall.
- **Confidence:** High

---

### ECU-COM-04: ClusterSerial schema not retransmitted after cluster reboot; cluster decodes D: frames without schema context

- **Severity:** Medium
- **Bug class:** Protocol state machine -- no recovery path for cluster-side reboot
- **Location:** `src/system/ClusterSerial.cpp:81-100` (`begin()`), `102-182` (`tick()`)
- **Description:** The full schema block (OT:, P:, M:, F:, L:, Z) is transmitted exactly once, inside `ClusterSerial::begin()`, which runs during ECU `setup()`. `ClusterSerial::tick()` only ever emits D: and S: frames. If the cluster display resets (power glitch, internal watchdog, firmware panic) it re-enters schema-parse mode and waits for `OT:<ver>` to resynchronise. Because the ECU is TX-only with no RX parser there is no way to detect the cluster reset, and no code path causes `_sendSchema()` to be called again at runtime.
- **Trigger:** Any cluster-side reset after ECU boot: power rail sag to the cluster during high-vibration operation, cluster firmware watchdog, intentional cluster reboot for a display firmware update while the ECU runs.
- **Impact:** After cluster reset, the cluster receives D: frames (e.g. `D:72000,650.0,3.50,1`) with no F: field mapping. The cluster cannot associate column positions to sensor identities and will either display garbage, blank the gauges, or enter an error state. In the worst case the cluster shows stale (last-known) or zeroed values while the engine is running -- the pilot loses instrument visibility without any indication of a comms fault.
- **Evidence:**
  - `ClusterSerial.cpp:43-78`: `_sendSchema()` exists only as a file-scope static called by `begin()`.
  - `ClusterSerial.cpp:103-182`: `tick()` has no schema-resend path.
  - Protocol v2 spec in `ClusterSerial.h:20-33`: `OT:<ver>` causes the cluster to enter schema-parse mode, implying the cluster has a reboot/resync path -- but the ECU does not exploit it.
- **Suggested fix:** Periodically re-transmit the schema (e.g. every 60 seconds) or provide a trigger mechanism. Because this is TX-only the ECU cannot detect a cluster reset, so periodic re-sends are the only option. The cluster firmware should be tolerant of receiving a second `OT:1` mid-session (entering schema-parse mode again, then returning to runtime on receipt of `Z`). A re-send interval of 60 s adds ~540 bytes (~47 ms at 115200) of periodic overhead -- acceptable.
- **Confidence:** High

---

### ECU-COM-05: snprintf truncation in ClusterSerial D: frame building is silently ignored; second call may underflow size argument

- **Severity:** Medium
- **Bug class:** Integer arithmetic on mixed signed/unsigned types; silent data corruption
- **Location:** `src/system/ClusterSerial.cpp:173-181`
- **Description:**
  ```cpp
  char pkt[64];
  int  n = snprintf(pkt, sizeof(pkt), "D:%.0f,%.1f,%.2f,%d",
      (double)ed.n1Rpm, (double)ed.tot,
      (double)ed.oilPressure, ed.flameDetected ? 1 : 0);
  if (HardwareConfig::hasN2Rpm) {
      n += snprintf(pkt + n, sizeof(pkt) - n, ",%.0f", (double)ed.n2Rpm);
  }
  (void)n;
  _port.println(pkt);
  ```
  Two issues:

  1. **Silent truncation.** `snprintf` returns the number of bytes the format string *would have* written (excluding the null terminator), not the number actually written. If the first call returns a value >= 64 (buffer full, content truncated), `n` holds that larger value. The second call then computes `sizeof(pkt) - n`: because `sizeof(pkt)` is `size_t` (unsigned, value 64) and `n` is `int`, the subtraction is evaluated in `size_t` arithmetic. If `n > 64`, the result wraps to a large unsigned value and the second `snprintf` is handed a pointer past the end of `pkt` with a multi-gigabyte size -- undefined behaviour and a potential stack overwrite. The truncated, semantically wrong frame is then sent to the cluster via `println()`. The `(void)n` suppresses the only signal that truncation occurred.

  2. **No error path for snprintf returning -1.** If `snprintf` encounters an encoding error it returns -1. `pkt + (-1)` is UB, and `size_t(64) - (-1)` wraps to 65 (or a huge value on 64-bit), passing an unclamped size to the second call.

- **Trigger:**
  - Issue 1: Realistic for sensor-value corruption: a floating-point NaN or Inf propagated from a failed ADC read into `ed.n1Rpm` formats as "nan", "inf", or "-inf" (3-4 bytes), which keeps the packet within 64 bytes. However an uncalibrated or overflowing raw ADC could produce values >= 1e10 which format as 11+ digit strings. Under normal sensor health this does not trigger.
  - Issue 2: Only if the `%f` format encounters a locale/encoding failure -- effectively impossible on bare-metal Arduino but not provably unreachable.

- **Impact:** If triggered: a malformed D: frame is sent to the cluster (wrong gauge values) and the second `snprintf` call exhibits undefined behaviour that may overwrite adjacent stack variables. The frame is transmitted regardless of truncation, corrupting the cluster display.

- **Evidence:**
  - `ClusterSerial.cpp:174-180`: `n` computed from `snprintf`, used as pointer offset and size-subtraction operand without bounds check; result discarded via `(void)n`.

- **Suggested fix:**
  ```cpp
  char pkt[64];
  int n = snprintf(pkt, sizeof(pkt), "D:%.0f,%.1f,%.2f,%d",
      (double)ed.n1Rpm, (double)ed.tot,
      (double)ed.oilPressure, ed.flameDetected ? 1 : 0);
  if (n > 0 && n < (int)sizeof(pkt) && HardwareConfig::hasN2Rpm) {
      snprintf(pkt + n, sizeof(pkt) - (size_t)n, ",%.0f", (double)ed.n2Rpm);
  }
  if (n <= 0 || n >= (int)sizeof(pkt)) {
      // log truncation / format error; skip sending
      return;
  }
  _port.println(pkt);
  ```
  Additionally, validate that sensor floats are finite before formatting: `if (!isfinite(ed.n1Rpm)) ed.n1Rpm = 0.0f;` or equivalent in the sensor layer.

- **Confidence:** Medium (overflow only with unrealistic float values; type mismatch is real and present in source)

---

### ECU-COM-06: CommandQueue comment incorrectly documents producer set; DI handler and start switch on Core 1 also push

- **Severity:** Low
- **Bug class:** Documentation / latent design confusion (queue itself is safe)
- **Location:** `src/system/CommandQueue.h:7`, `src/main.cpp:624,1456`
- **Description:** The CommandQueue header comment states "thread-safe one-way pipe: Web (Core 0) -> ECU (Core 1)" and the `push()` docstring reads "Called from Core 0 (web handler)". In practice `push()` is also called from Core 1 in two places:
  - `main.cpp:624`: DI `ab_fire` role handler inside `checkGeneralDI()`.
  - `main.cpp:1456`: `checkStartSwitch()` pushing `OTCommand::START`.
  Both run inside the Core 1 `loop()`. FreeRTOS `xQueueSendToBack()` is internally mutex-protected and safe for simultaneous multi-core or multi-producer use, so there is no actual race condition. The bug is that `push()` and `drain()` can be called from the same core in the same tick: `drain()` runs at line 1624, `checkGeneralDI()` runs at line 1653 -- meaning a DI-pushed AB_FIRE is not processed until the *following* tick. This one-tick lag is generally benign but is invisible from the comment, and any future developer who reads "Core 0 only" and moves DI handling to a higher-priority ISR would introduce a real problem.
- **Trigger:** Any DI ab_fire event or start switch edge.
- **Impact:** One main-loop tick of latency for DI-sourced commands (typically ~1-2 ms). No data corruption.
- **Evidence:**
  - `CommandQueue.h:67`: `// Called from Core 0 (web handler)`.
  - `main.cpp:624,1456`: `CommandQueue::push()` called from Core 1 functions.
- **Suggested fix:** Update the comment to "Called from Core 0 (web) and Core 1 (DI handler, start switch) -- FreeRTOS queue is multi-producer safe." Add a note that DI-pushed commands are processed on the next tick.
- **Confidence:** High (behaviour confirmed by source; severity is low because no runtime fault)

---

### ECU-COM-07: _crc16() helper function is dead code -- never called

- **Severity:** Low
- **Bug class:** Hygiene / dead code
- **Location:** `src/system/MAVLinkOutput.h:70-87`
- **Description:** `MAVLinkOutput` declares and implements a private static helper `_crc16(const uint8_t* buf, size_t len, uint8_t extra)` that computes CRC-16/MCRF4XX (X25) over a buffer with a CRC_EXTRA byte. The comment at line 100-101 explicitly notes that calling `_crc16()` with `extra=0` is not a no-op and so the CRC is computed inline inside `_sendPacket()` instead. As a result `_crc16()` is never referenced anywhere and will be elided by the compiler with a warning (or silently on most embedded builds without `-Wall -Wunused-function`).
- **Trigger:** Always present; no runtime effect.
- **Impact:** Code confusion -- a reader auditing CRC correctness finds the helper and may assume it is used. The actual CRC logic is the inline code in `_sendPacket()` at lines 102-122.
- **Evidence:**
  - `MAVLinkOutput.h:70-87`: function definition.
  - No call site exists in the file.
- **Suggested fix:** Remove `_crc16()` entirely, or consolidate: refactor `_sendPacket()` to call `_crc16()` for the header bytes and payload, passing `crcExtra` as the extra argument (since `crcExtra != 0` for all three message types, the original concern about `extra=0` being silently a no-op does not apply to any current call site).
- **Confidence:** High

---

## Notes / unclear areas

1. **HardwareSerial TX buffer size.** The 256-byte figure used in ECU-COM-03 is the ESP32 Arduino HardwareSerial default (`UART_FIFO_LEN + 256`). If the integrator calls `_mavSerial.setTxBufferSize(N)` elsewhere the analysis changes. No such call was found in the reviewed files; if one exists in a hardware_profile.h that was not reviewed, ECU-COM-03's blocking duration changes accordingly.

2. **ClusterSerial `_port.println()` blocking when cluster is disconnected.** At 115200 baud and a 31-byte D: frame the UART hardware continues clocking out bits regardless of whether a receiver is connected. The 31-byte payload fits well within the 256-byte TX buffer. With a 50 ms interval the buffer drains between ticks. No blocking observed under normal operation; blocking would only occur if `clusterIntervalMs` were set much lower than 1 ms (each frame takes ~2.8 ms to clock out at 115200).

3. **MAVLink `sendStatusText()` is not rate-limited.** It is called directly from `enterFaultShutdown()` (Core 1 only) and bypasses the `mavlinkIntervalMs` gate. In a fault scenario where `enterFaultShutdown()` could be re-entered rapidly (e.g. sequencer calling it on each block timeout), multiple STATUSTEXT messages would be sent back-to-back. A single STATUSTEXT is 59 bytes -- manageable -- but if `enterFaultShutdown()` can be called in a tight loop the same TX stall risk as ECU-COM-03 applies. Review of the sequencer guard logic was not in scope; flagged for follow-up.

4. **ClusterSerial `L:` line uses `Config::rpmLimit`, `Config::totLimit`, etc. directly.** If these Config values change at runtime via APPLY_CONFIG while the cluster is displaying (only permissible in STANDBY per the command handler), the cluster's cached gauge limits from the boot-time `L:` line will be stale. This is acceptable given APPLY_CONFIG is STANDBY-only and the ECU would need to re-call `begin()` to push updated limits -- but that re-triggers the 300 ms delay. No in-scope fix required; informational.

5. **OTPacket `fParam` and `iParam` are not range-validated before `push()`** for commands like `SET_OIL_DEMAND` (float bar target) or `SET_OIL_PCT` (int percent). The web server reads these directly from JSON (`doc["fParam"] | 0.0f`). Validation happens inside `handleCommand()` on Core 1, which is the correct layer. No comms-layer finding here.
