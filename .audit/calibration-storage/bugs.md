# Calibration-storage subsystem audit

## Files reviewed

- `/home/user/openturbine/src/system/Config.h`
- `/home/user/openturbine/src/system/Config.cpp`
- `/home/user/openturbine/src/system/HardwareConfig.h`
- `/home/user/openturbine/src/system/HardwareConfig.cpp`
- `/home/user/openturbine/partitions.csv`
- `/home/user/openturbine/src/platform/esp32/PlatformInit.h`
- `/home/user/openturbine/src/system/web/WebServer.cpp`
- `/home/user/openturbine/src/main.cpp` (sequence validation, requestSave call sites)
- `/home/user/openturbine/src/Hardware.h` (applyConfig, safety limit plumbing)

---

## Findings

### ECU-CAL-01: Off-by-one allows one-byte overflow in g_webRxBuf

- **Severity:** High
- **Bug class:** Memory safety (off-by-one, heap-adjacent static buffer write)
- **Location:** `WebServer.cpp` lines 473, 502, 659, 877, 926, 987 -- every POST/PATCH body accumulation loop
- **Description:** The accumulation guard is `if (g_webRxLen + len < sizeof(g_webRxBuf))`. The correct safe bound is `<=`, not `<`. When exactly `sizeof(g_webRxBuf)` bytes arrive (8192 bytes total), the condition is false, `g_webRxOverflow` is set, and the data is not written -- that part is correct. However, when `g_webRxLen + len == sizeof(g_webRxBuf) - 1` and then one more byte arrives with `len == 1`, the body now fills the buffer exactly. The `memcpy` copies into `g_webRxBuf + g_webRxLen` with `len == 1`, bringing `g_webRxLen` to 8192. The buffer is `char[8192]`, valid indices 0..8191, so `g_webRxBuf[8191]` is the last valid byte. This specific path is actually safe because `<` prevents writing when the write would start at or past the end. The real hazard is the null terminator: `Config::fromJson` and `HardwareConfig::fromJson` call `deserializeJson(doc, json, len)` where `len` is `g_webRxLen` (up to 8192). ArduinoJson 7 does not require a null terminator when given an explicit length, so this is safe for the JSON parse itself. However `Config::toJson(char* buf, size_t len)` calls `serializeJson(doc, buf, len)` on `g_webTxBuf[8192]`; if the serialised output exactly fills 8192 bytes, `serializeJson` null-terminates at `buf[8192]`, one byte past the buffer. The WebServer already detects and logs this (`n >= sizeof(g_webTxBuf)`) but does not abort the response -- it sends truncated JSON to the client and continues. A truncated hardware-section GET response could confuse a PATCH merge and write a corrupted config silently.
- **Trigger:** Hardware config or settings config that serialises to exactly or more than 8192 bytes.
- **Impact:** Truncated GET response fed back through PATCH merge produces corrupt config. One-byte write past `g_webTxBuf` is an adjacent-static-buffer corruption that could corrupt `_server` or `_ws` objects depending on linker layout.
- **Evidence:** `WebServer.cpp:460-462`, `WebServer.cpp:863-866`, `WebServer.cpp:943-945`.
- **Suggested fix:** Increase `g_webTxBuf` to 12288 bytes (hardware JSON with all fields is ~6 KB, settings JSON is ~5 KB). On the GET path, abort and return HTTP 500 when `n >= sizeof(g_webTxBuf)` rather than only logging. Change the null-terminator risk to zero by using `serializeJson(doc, buf, len - 1)` and always null-terminating explicitly.
- **Confidence:** High

---

### ECU-CAL-02: HardwareConfig::save does not use atomic rename -- power loss corrupts both sections

- **Severity:** High
- **Bug class:** Fail-safe / persistence
- **Location:** `HardwareConfig.cpp:376-394`
- **Description:** `Config::save()` uses write-to-tmp then rename (partially atomic -- see ECU-CAL-03). `HardwareConfig::save()` opens `ecu_config.json` directly for writing: `LittleFS.open(PATH, "w")`. This truncates the file in place. If power is lost after the `open()` call but before `serializeJsonPretty` completes, the file is left truncated or partially written. On next boot, `Config::load()` and `HardwareConfig::load()` both attempt to read the same file and get a JSON parse error. Both fall back to compile-time defaults. The result is that ALL calibration, ALL safety limits, and ALL pin assignments revert to defaults -- not just the hardware section.
- **Trigger:** Power interruption during a POST /api/hardware or PATCH /api/hardware save.
- **Impact:** Critical -- engine boots with compile-time default pin mapping and safety limits regardless of user calibration. If the user had configured non-default actuator pins, wrong GPIO lines are toggled.
- **Evidence:** `HardwareConfig.cpp:386-393`.
- **Suggested fix:** Apply the same write-to-tmp-then-rename pattern used in `Config::save()`. Use a distinct temp path such as `/ecu_config_hw.tmp`.
- **Confidence:** High

---

### ECU-CAL-03: Atomic rename window -- remove succeeds but rename fails, leaving no config file

- **Severity:** High
- **Bug class:** Fail-safe / persistence
- **Location:** `Config.cpp:369-373`
- **Description:** `Config::save()` deletes the current `ecu_config.json` with `LittleFS.remove(PATH)` and then calls `LittleFS.rename(TMP_PATH, PATH)`. The comment acknowledges the window: "if power dies here, next boot finds no config file and loads safe defaults." This is only partially safe. The defaults for rpmLimit (100000) and totLimit (750) are conservative. However the defaults for oil polynomial coefficients are `oilPolyA/B/C/D = 0`, meaning the oil pressure sensor returns 0 bar for every ADC reading. If `safetyLowOil` is enabled (it is by default), the engine immediately triggers LOW_OIL_PRESSURE on every run. Additionally, if the calibrated `throttleMinRaw`/`throttleMaxRaw` differ significantly from the defaults (950/3150), the throttle curve becomes wrong silently. The rename can also fail on LittleFS when flash is full (LittleFS rename is not guaranteed atomic across a block boundary on all versions). The failure is logged but `_savePending` has already been cleared (line 342), so the save is not retried.
- **Trigger:** Power loss between `remove` and `rename`, or flash full during rename.
- **Impact:** High -- engine starts with zeroed oil polynomial (always reads 0 bar), tripping low-oil safety on first use. totalRunSeconds is silently lost.
- **Evidence:** `Config.cpp:340-346`, `Config.cpp:369-374`.
- **Suggested fix:** Clear `_savePending` only after `save()` returns true. On failure, leave the flag set so the next `flushPendingSave()` retries. For the rename-after-remove window, consider copying the tmp file back if rename fails (LittleFS supports this). Alternatively, keep a second backup copy of the last known-good file.
- **Confidence:** High

---

### ECU-CAL-04: _savePending is volatile but not atomic -- Core 1 write / Core 0 read race

- **Severity:** Medium
- **Bug class:** Concurrency
- **Location:** `Config.h:347`, `Config.cpp:332-346`
- **Description:** `_savePending` is declared `volatile bool`. On Xtensa LX6 (ESP32 dual-core), `volatile` prevents the compiler from caching the value in a register but does NOT provide an atomic read-modify-write or a memory barrier. Core 1 (`requestSave()`) writes `_savePending = true`. Core 0 (`flushPendingSave()`) reads it, then writes it to false, then calls `save()`. Because the ESP32 does not guarantee coherent caches for `volatile` without a barrier, Core 0 could read a stale `false` and skip the save, losing the totalRunSeconds update for that session. The reverse race (Core 0 clears the flag just as Core 1 sets it) also exists: Core 0 reads true, clears to false, then Core 1 sets it back to true for a different update; Core 0's in-progress `save()` captures the values from that second Core 1 update, but `_savePending` is now true again and will trigger a redundant re-save. This is benign but wastes flash writes.
- **Trigger:** High-frequency requestSave calls from Core 1 (every ~1 s during RUNNING) while Core 0 is simultaneously flushing.
- **Impact:** Medium -- occasional totalRunSeconds not persisted (hour meter loses up to ~1 s per race). No engine-safety impact.
- **Evidence:** `Config.cpp:334-345`, `main.cpp:996-997`.
- **Suggested fix:** Use `std::atomic<bool> _savePending` with `memory_order_relaxed` for the set and `memory_order_acquire`/`memory_order_release` for the flush. Alternatively, use `portENTER_CRITICAL_ISR` / `portEXIT_CRITICAL_ISR` around the flag manipulation since this crosses a core boundary. On Xtensa, a simple `__atomic_store_n` / `__atomic_load_n` with `__ATOMIC_SEQ_CST` is also sufficient.
- **Confidence:** Medium (Xtensa cache coherency for volatile globals is implementation-defined; in practice the ESP-IDF linker places statics in DRAM which is cache-coherent, but the C++ memory model provides no guarantee)

---

### ECU-CAL-05: No input validation on safety-critical float fields -- rpmLimit=0 accepted

- **Severity:** High
- **Bug class:** Input validation / fail-safe
- **Location:** `Config.cpp:_fromDoc()` lines 508-512; `Config::fromJson(const char*, size_t)` lines 395-406
- **Description:** There is no range check on any config field before it is applied to the runtime struct. A POST /api/config body with `{"profile_id":"<OT_PROFILE_ID>","engine":{"rpm_limit":0,"tot_limit":0}}` is accepted, saved, and immediately applied to `Config::rpmLimit` and `Config::totLimit`. These values are copied into `g_safety.rpmLimit` and `g_safety.totLimit` on the next `Hardware::applyConfig()` call (Hardware.h:511,513). With `rpmLimit = 0`, `safetyOverspeed` fires immediately on any nonzero N1 reading, triggering an emergency shutdown every time the engine tries to start. With `tot_limit = 0`, any positive TOT triggers overtemp. Negative values are also accepted (e.g., `rpmLimit = -1`), which disables the overspeed check because the comparison `ed.n1Rpm > rpmLimit` is always true for any running RPM, meaning the engine never trips the limit regardless of actual speed. Similarly, `oilPolyA/B/C/D` are accepted without bounds, allowing an attacker or misconfigured UI to inject polynomial coefficients that produce arbitrarily large oil pressure readings, masking a real low-oil condition.
- **Trigger:** Malformed POST /api/config while engine is in STANDBY (or any state in OT_DEV_MODE).
- **Impact:** High -- overspeed protection silently disabled, or engine locked in boot loop through repeated trips.
- **Evidence:** `Config.cpp:508`, `Config.cpp:403-405`; `Hardware.h:511`.
- **Suggested fix:** Add a `validate()` method that enforces: `rpmLimit >= 10000`, `totLimit >= 300`, `oilRunningMin >= 0.1`, pin numbers in valid ESP32 range (0-39 for inputs, 0-33 for outputs). Reject (return false from `fromJson`) if any safety limit is outside sensible bounds. Apply the same validation in `_fromDoc` at load time; if a value fails validation, emit a warning and keep the compile-time default rather than applying the bad value.
- **Confidence:** High

---

### ECU-CAL-06: HardwareConfig::fromJson has no isLocked / engine-state guard

- **Severity:** Medium
- **Bug class:** Concurrency / input validation
- **Location:** `HardwareConfig.cpp:599-605`, `WebServer.cpp:895`, `WebServer.cpp:968`
- **Description:** `HardwareConfig::fromJson()` immediately writes into static fields (pin numbers, actuator types, feature flags) with no check on engine state. The POST /api/hardware handler (WebServer.cpp:890) correctly checks `mode != STANDBY` before calling `fromJson`. However the PATCH /api/hardware handler (WebServer.cpp:920) checks the state at the START of the body-accumulation callback but does NOT re-check at the end, after all chunks are received. In an AsyncWebServer environment, the state check runs on the first chunk; the engine could transition from STANDBY to STARTUP between the first and last chunk of a multi-chunk PATCH body. At that point `fromJson` and `save` are called while the engine is in STARTUP, potentially changing pin polarity or actuator type mid-sequence. Additionally, `HardwareConfig::fromJson()` itself has no internal lock check, so callers cannot rely on the function to self-guard.
- **Trigger:** Slow multi-chunk PATCH upload (possible on congested WiFi) concurrent with operator pressing START.
- **Impact:** Medium -- actuator pin polarity or pump type changed while engine is running could cause unexpected output changes (e.g., inverting fuel solenoid active-high flag flips the solenoid state).
- **Evidence:** `WebServer.cpp:914-970` -- state check at line 920 is outside the body-complete block, which only runs when `index + len == total`.
- **Suggested fix:** Move the `mode != STANDBY` check to after `if (index + len < total) return;` in the PATCH handler, mirroring the structure in POST /api/hardware. Add an `isLocked()` equivalent to `HardwareConfig` that the function itself can check.
- **Confidence:** High

---

### ECU-CAL-07: Oil pressure polynomial defaults are all-zero -- oil sensor reads 0 bar on first boot

- **Severity:** High
- **Bug class:** Fail-safe / calibration
- **Location:** `Config.cpp:197-201`, `Config.cpp:482-483`
- **Description:** `oilPolyA`, `oilPolyB`, `oilPolyC`, and `oilPolyD` default to 0. The polynomial is evaluated as `A*x^3 + B*x^2 + C*x + D` (Hardware.h:499-501). With all coefficients zero, the result is always 0 bar regardless of ADC reading. On first boot (no config file), `_applyDefaults()` is called and then `save()` writes these zero coefficients to flash. If `hasOilPress = true` (the default) and `safetyLowOil = true` (the default), `Config::oilRunningMin = 2.8` bar, then the engine reads 0 bar oil pressure on every tick and immediately trips LOW_OIL_PRESSURE in RUNNING. This makes the engine unrunnable until the user calibrates the oil polynomial via the web UI. There is no warning at boot or in the logs that the polynomial is uncalibrated; `oilPolyXMin=0` and `oilPolyXMax=4095` are populated but the coefficients producing the output are all zero.
- **Trigger:** First boot on a fresh device, or any config reset to defaults.
- **Impact:** High -- engine cannot run after first-boot or factory reset without manual oil calibration. The fault appears to the user as a spurious LOW_OIL fault with no explanation.
- **Evidence:** `Config.cpp:197-201`; `HardwareConfig.cpp:29` (`hasOilPress = true`); `HardwareConfig.cpp:277` (`safetyLowOil = true`).
- **Suggested fix:** Either set a safe non-zero default polynomial that approximates a typical linear oil pressure sensor (e.g., `oilPolyC = oilRunningMin / 2048` for a midrange reading), or emit a prominent boot warning when all polynomial coefficients are zero and `hasOilPress` is true. Consider disabling `safetyLowOil` in the compiled defaults and requiring the user to enable it after calibration. Document the required calibration step in the startup sequence validation output.
- **Confidence:** High

---

### ECU-CAL-08: totalRunSeconds uint32_t overflow at ~136 years -- negligible in isolation, but accumulation is additive-only

- **Severity:** Low
- **Bug class:** Integer overflow
- **Location:** `Config.h:254`, `main.cpp:995-997`
- **Description:** `totalRunSeconds` is `uint32_t`, which overflows at 4,294,967,295 seconds (~136 years). Overflow is not safety-critical for this specific field. However the accumulation logic at `main.cpp:996` is `Config::totalRunSeconds += elapsed` where `elapsed` is also `uint32_t`. If `millis()` overflows its own `unsigned long` (~49 days of runtime), `elapsed` could be a very large number that causes `totalRunSeconds` to jump by millions of seconds in one tick. `millis()` overflows cleanly to 0 on ESP32, so `millis() - _runStartMs` wraps correctly for intervals less than 49 days. The real risk is that `_runStartMs` is set at engine start and if the engine runs for >49 continuous days (unlikely for a turbine ECU) the computed `elapsed` would be incorrect. A separate minor issue: the `requestSave()` is called every time this field increments, but `flushPendingSave()` is called from `WebServer::tick()` which runs on Core 0. If Core 0 is blocked (e.g., large OTA, log flush), multiple requestSave() calls accumulate but only one save happens. This is correct behavior but worth noting that the last save could be minutes after the last engine stop.
- **Trigger:** Engine runtime exceeding 49 continuous days (extremely unlikely).
- **Impact:** Low -- spurious hour meter inflation only; no safety impact.
- **Evidence:** `Config.h:254`; `main.cpp:995-997`.
- **Suggested fix:** Record the risk in a comment. If needed, use a monotonic clock or cap elapsed to a reasonable maximum (e.g., 3600 seconds per flush).
- **Confidence:** Medium

---

### ECU-CAL-09: LittleFS.begin(true) silently formats flash on mount failure -- destroys all calibration data

- **Severity:** Medium
- **Bug class:** Fail-safe / data loss
- **Location:** `PlatformInit.h:23`
- **Description:** `LittleFS.begin(true)` is called with `formatOnFail = true`. If the LittleFS partition fails to mount (e.g., due to a power loss during a flash write, a LittleFS library version mismatch, or a new firmware that changes the partition table), the entire filesystem is silently formatted and all user calibration data, hardware config, and flight logs are permanently erased. The user gets no warning beyond a serial message `"LittleFS mount failed -- formatted"` which is visible only over UART, not via the web UI. On the next boot, the engine starts with all defaults (see ECU-CAL-07 for the zero-polynomial problem that follows).
- **Trigger:** LittleFS mount failure after flash filesystem corruption or partition table change.
- **Impact:** Medium -- total calibration data loss; engine becomes unrunnable until recalibrated (see ECU-CAL-07). Flight log history is also erased.
- **Evidence:** `PlatformInit.h:23`.
- **Suggested fix:** Set `formatOnFail = false` in production builds. If mount fails, set a boot fault (`EngineData::faultDescription`) and halt before loading config. Provide a separate "Format filesystem" action in the web UI that requires explicit confirmation. Reserve `formatOnFail = true` for factory / development builds only, gated behind `OT_DEV_MODE`.
- **Confidence:** High

---

### ECU-CAL-10: wifiPassword stored in plaintext in ecu_config.json and serialised by GET /api/hardware

- **Severity:** Medium
- **Bug class:** Security / information disclosure
- **Location:** `HardwareConfig.cpp:611`, `HardwareConfig.cpp:886-889`, `WebServer.cpp:862-868`
- **Description:** `HardwareConfig::wifiPassword[32]` is written verbatim into `ecu_config.json` as `hardware.wifi_password`. It is also returned verbatim in the GET /api/hardware response. Any client that can reach the web UI (which serves an open AP by default when `wifiPassword` is empty) can read the configured password. Since the password controls access to the AP itself, this is circular: once connected, you can retrieve the credential. More critically, the password is stored in the LittleFS filesystem without any encryption, so physical flash read access (e.g., using a JTAG adapter) extracts the password trivially. The field is also part of the backup that can be downloaded via GET /api/ecu_config, creating an unintended credential-export vector.
- **Trigger:** Any authenticated web client performing GET /api/hardware or GET /api/ecu_config.
- **Impact:** Medium -- credential exposure; low practical risk given the ESP32's local-AP model, but violates least-privilege for a safety-critical embedded device.
- **Evidence:** `HardwareConfig.cpp:611`, `WebServer.cpp:862-868`.
- **Suggested fix:** Exclude `wifi_password` from the GET /api/hardware and GET /api/ecu_config responses (replace with a boolean `wifi_password_set`). Accept the field on POST but never echo it back. Store using ESP32 NVS Preferences namespace (which uses flash encryption when enabled) rather than plaintext in LittleFS.
- **Confidence:** High

---

### ECU-CAL-11: POST /api/config does not re-check isLocked after body accumulation completes

- **Severity:** Medium
- **Bug class:** Concurrency / TOCTOU
- **Location:** `WebServer.cpp:479-493`
- **Description:** The POST /api/config body handler checks `!Config::isLocked()` only after all chunks have been received and `g_webRxOverflow` is false. This is correct in principle, but the body accumulation may span multiple AsyncWebServer callbacks. During accumulation (first chunk through last chunk), the engine state is not checked. If the engine transitions from STANDBY to STARTUP between the first and last chunk of a large POST body, the final check at line 484 will see `STARTUP` and correctly reject with HTTP 423. However, the check is `if (!Config::isLocked()) { ... } else { req->send(423, ...) }`. The rejection is correct. The TOCTOU is in the opposite scenario for PATCH /api/config: if `isLocked()` returns false at line 513, the code proceeds to deserialise and merge, but the engine state could change to RUNNING between lines 513 and 537 (the `Config::fromJson(current)` call which calls `save()`). The `fromJson(const JsonDocument&)` overload does not recheck `isLocked()` -- it calls `_fromDoc` and `save()` unconditionally.
- **Trigger:** Engine state change (STANDBY to STARTUP) during a slow multi-chunk PATCH /api/config upload.
- **Impact:** Medium -- config written to flash while engine is in STARTUP. Safety limits (rpmLimit, totLimit) are updated mid-sequence. The sequencer reads them on next applyConfig(), which is called at the start of each START command -- so the risk is limited to a very narrow window, but the write to flash during STARTUP is undesirable.
- **Evidence:** `Config.cpp:408-412` -- `fromJson(const JsonDocument&)` calls `save()` without locking check.
- **Suggested fix:** Add `if (isLocked()) return false;` to the top of `Config::fromJson(const JsonDocument& doc)`. The PATCH handler should also recheck `isLocked()` immediately before calling `fromJson`.
- **Confidence:** High

---

### ECU-CAL-12: Legacy config migration calls save() before profileId is set -- can write wrong profile_id into unified file

- **Severity:** Medium
- **Bug class:** Logic bug / state machine
- **Location:** `Config.cpp:245-249`
- **Description:** In the legacy-file migration path, when a profile mismatch is detected in the legacy `/config.json`, the code calls `save()` at line 245 to write defaults into the unified file, then removes the legacy file. At the point of the `save()` call, `profileId` still contains the mismatched ID read from the legacy file (line 238 set it, line 240 confirmed mismatch). The `_toDoc()` function writes `OT_PROFILE_ID` (the compile-time constant) not `profileId`, so the serialised JSON gets the correct firmware profile ID. This is safe. However, the subsequent `strncpy(profileId, OT_PROFILE_ID, ...)` at line 247 overwrites the field after the save, meaning that if any code between lines 245 and 247 reads `profileId` it sees the wrong (legacy) value. In the current code there is no such read, but this ordering is fragile and could be broken by future edits. The same pattern occurs in `Config.cpp:313-317` for the unified-file mismatch path.
- **Trigger:** Device flashed with a different firmware profile than the stored config. Occurs on profile migration or accidental cross-profile flash.
- **Impact:** Medium -- currently benign because `_toDoc` uses `OT_PROFILE_ID` directly. If a future change reads `profileId` in `_toDoc`, the wrong profile would be persisted.
- **Evidence:** `Config.cpp:238-249`.
- **Suggested fix:** Set `profileId` to `OT_PROFILE_ID` before calling `save()` in all mismatch-recovery branches. Make `_toDoc` always use `profileId` (already correctly set) rather than `OT_PROFILE_ID` directly, so that the two sources of truth are collapsed to one.
- **Confidence:** Medium

---

## Notes / unclear areas

1. **No CRC/HMAC on stored config.** A bit flip in `ecu_config.json` could silently change rpmLimit or a pin number. ArduinoJson will parse it as valid JSON if the numeric value happens to remain well-formed after corruption. The current design relies entirely on LittleFS internal wear-levelling checksums; there is no application-layer integrity check.

2. **OT_DEV_MODE bypasses isLocked completely.** `Config::isLocked()` returns `false` whenever `ed.devMode` is true (Config.cpp:380). `devMode` is set at boot by `OT_DEV_MODE` compile flag but can also be set at runtime via the web UI (doc["dev_mode"] at WebServer.cpp:156 implies a toggle endpoint exists). If dev mode can be enabled while the engine is RUNNING via a web command, all config write paths become unguarded. The toggle endpoint was not in scope for this audit; its lock semantics should be verified.

3. **Sequence name validation is warning-only in `validateSequences()`.** An unrecognized block name (e.g., a typo in the JSON) results in `addIssue(..., isError=true)` and sets `ed.seqHasErrors`. Whether START is actually blocked depends on whether the startup code checks `ed.seqHasErrors` before accepting the command -- this was observed in `main.cpp:191-194` and the issue infrastructure is in place, but whether the engine strictly refuses to start was not fully traced.

4. **`HardwareConfig::fromJson` returns `false` only on JSON parse error, not on semantic validation.** A valid JSON document with `throttlePin = 255` (not a valid ESP32 GPIO) is accepted and stored. The `validate()` gap (ECU-CAL-05) applies equally to hardware topology fields.

5. **Flash size and LittleFS partition.** The `spiffs` partition (named "spiffs" but actually formatted as LittleFS per the code) is 0xE0000 bytes (896 KB). The two JSON config files together are expected to be under 30 KB. Session logs and flight recorder data share this space. If logs grow to fill the partition, subsequent `Config::save()` calls may fail silently when `LittleFS.open(TMP_PATH, "w")` returns a null handle (checked and returns false, but `_savePending` is already cleared -- see ECU-CAL-03).
