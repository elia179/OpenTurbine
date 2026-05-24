# RTOS-architecture subsystem audit

## Files reviewed

| File | Lines examined |
|------|---------------|
| `src/main.cpp` | 88-139, 600-626, 982-1000, 1140-1155, 1290-1333, 1428-1496, 1500-1598, 1618-1689 |
| `src/system/CommandQueue.h` | all |
| `src/system/CommandQueue.cpp` | all |
| `src/system/Watchdog.h` | all |
| `src/hal/RCInput.h` | all |
| `src/system/FlightRecorder.cpp` | all |
| `src/system/SessionLogger.cpp` | all |
| `src/platform/esp32/PlatformInit.h` | all |
| `src/engine/EngineData.h` | all |
| `src/Hardware.h` | 390-469 |
| `src/system/web/WebServer.cpp` | 40-60, 435-600, 640-720, 860-975, 1040-1145 |
| `src/system/Config.cpp` | 230-415 |
| `platformio.ini` | all |

---

## Findings (Critical -> Info)

---

### ECU-RTOS-01: FlightRecorder::clear() called on Core 1 with portMAX_DELAY -- can stall ECU loop past TWDT

- **Severity:** Critical
- **Bug class:** Concurrency / Watchdog miss
- **Location:** `src/system/FlightRecorder.cpp:199-203`, `src/main.cpp:1295-1296`
- **Description:** `FlightRecorder::clear()` takes `_mutex` with `portMAX_DELAY`. It is called directly from `handleCommand(CLEAR_LOG)` which runs on Core 1 inside the ECU loop (main.cpp:1295-1296). If Core 0 is simultaneously executing `FlightRecorder::runEviction()` (main.cpp:1598 webTask -> WebServer::tick -> runEviction) or `lockLog()` (called from async_tcp HTTP handler), both of which also take the mutex with `portMAX_DELAY`, Core 1 is blocked for the full LittleFS file-copy duration (~100-200 ms per comment in the code). The TWDT timeout is 5 s, so a single occurrence is unlikely to trip it, but a series of CLEAR_LOG commands interleaved with concurrent file operations (eviction + log download) can push Core 1 past the 5 s window without a `Watchdog::feed()` call.
- **Trigger:** Browser issues CLEAR_LOG command while `/api/log` or eviction is in progress on Core 0.
- **Impact:** TWDT-triggered hard reset mid-run. Engine shutdowns safely (reset fires before WDT fires), but abrupt reset is unacceptable in flight.
- **Evidence:** `FlightRecorder.cpp:200` -- `xSemaphoreTake(_mutex, portMAX_DELAY)`. `FlightRecorder.cpp:292` -- `_append` uses `pdMS_TO_TICKS(8)` (safe), but `clear()` at line 200 does not. `main.cpp:1295-1296` -- called from Core 1 command handler.
- **Suggested fix:** Replace `portMAX_DELAY` in `clear()` with the same short timeout used in `_append` (`pdMS_TO_TICKS(8)`). Alternatively, push CLEAR_LOG execution to Core 0 via a flag (same pattern as `_evictionPending`).
- **Confidence:** High

---

### ECU-RTOS-02: webTask spawned before Watchdog::begin -- no WDT coverage during multi-second WiFi init

- **Severity:** High
- **Bug class:** Watchdog coverage gap / boot order
- **Location:** `src/main.cpp:1598` (xTaskCreatePinnedToCore), `src/main.cpp:1613` (Watchdog::begin)
- **Description:** `xTaskCreatePinnedToCore(webTask, ...)` is called at line 1598. `Watchdog::begin()` is not called until line 1613, after `FlightRecorder::logBoot()`, `ClusterSerial::begin()` (which includes a `delay()` call per comment at 1603), and the MAVLink serial init. During this window, the already-running webTask calls `WebServer::begin()` -> `_startWiFi()`, which blocks on WiFi AP bring-up. If WiFi hangs (e.g., driver deadlock in IDF5 on a brown-out boot), neither core is being watched. Additionally, the comment at line 1594 explicitly states "Stack needs to hold char buf[5120] + ArduinoJson + call frames from webTask tick" -- `WebServer::begin()` is the most stack-intensive call and happens in this unwatched window.
- **Trigger:** Boot after power brownout or on first-run WiFi init failure.
- **Impact:** Board hangs permanently at boot with no watchdog recovery. TWDT is the only hardware-level recovery mechanism; it is not armed during this gap.
- **Evidence:** `main.cpp:1598` vs `main.cpp:1613`. `ClusterSerial::begin()` uses `delay()` before WDT is armed.
- **Suggested fix:** Call `Watchdog::begin()` before `xTaskCreatePinnedToCore`. The TWDT subscription is per-task, so the main/setup task being subscribed does not interfere with setup() completing normally. Alternatively, give `Watchdog::begin()` a longer startup timeout (e.g., 15 s) and arm it immediately after `PlatformInit::begin()`.
- **Confidence:** High

---

### ECU-RTOS-03: Hardware::applyConfig() called from Core 0 (web PATCH handler via APPLY_CONFIG path) and Core 1 simultaneously -- unsynchronised write to block instances

- **Severity:** High
- **Bug class:** Concurrency / data race
- **Location:** `src/Hardware.h:396`, `src/main.cpp:1149` (Core 1 on START), `src/main.cpp:1327` (Core 1 on APPLY_CONFIG), `src/system/web/WebServer.cpp:538` (push APPLY_CONFIG via CommandQueue, safe), but see below for the direct path
- **Description:** `Hardware::applyConfig()` writes ~50 scalar fields into block-instance objects (`g_blkOilPrime`, `g_blkStarterSpin`, etc.) which are static globals allocated in a single translation unit. These writes happen on Core 1 via `handleCommand(APPLY_CONFIG)` (main.cpp:1327) and on every `START` (main.cpp:1149). However, `PATCH /api/hardware` at WebServer.cpp:968 calls `HardwareConfig::fromJson()` and `HardwareConfig::save()` directly on Core 0 (inside async_tcp callback) -- it does NOT push through CommandQueue. The block instances read `Config::*` and `HardwareConfig::*` static fields inside their `onEnter` / `tick` methods which also run on Core 1 concurrently. While individual 32-bit reads are atomic on Xtensa, the compound multi-field update in `applyConfig()` is not. If Core 1 executes a block `onEnter` while Core 0 is halfway through `applyConfig()`, some block fields will have new values and some old ones.
- **Trigger:** Operator applies a PATCH /api/hardware calibration update while an engine startup sequence is in progress.
- **Impact:** Inconsistent startup-block configuration (e.g., starter demand from new config, starter timeout from old config). Could cause premature abort or extended starter operation.
- **Evidence:** `Hardware.h:396-469` -- applyConfig writes ~50 fields. `WebServer.cpp:968` -- `HardwareConfig::fromJson()` directly on async_tcp Core 0 with no queue. `main.cpp:1327` -- applyConfig on Core 1.
- **Suggested fix:** The PATCH /api/hardware handler already rejects requests when mode != STANDBY (WebServer.cpp:920), so a race during active startup is currently blocked at the API level. However, this is an unenforced convention: the APPLY_CONFIG push at line 538 is for the config (settings) PATCH, not the hardware PATCH. Verify the mode check at line 920 cannot be bypassed (e.g., race between mode check and engine start). Add a build-time comment or static assert to document that `applyConfig()` must only be called on Core 1 or with engine in STANDBY.
- **Confidence:** Medium (API-level guard exists but is not enforced in the function itself)

---

### ECU-RTOS-04: SessionLogger::startSession() and endSession() call vTaskDelay() on Core 1 -- feeds watchdog indirectly but can inject 30 ms jitter into control loop

- **Severity:** High
- **Bug class:** Timing / control loop jitter
- **Location:** `src/system/SessionLogger.cpp:116`, `src/system/SessionLogger.cpp:168`
- **Description:** Both `startSession()` (called at engine START on Core 1, main.cpp:1151) and `endSession()` (called at enterStandby on Core 1, main.cpp:990) call `vTaskDelay(pdMS_TO_TICKS(30))` to "let Core 0 finish any in-progress drain". During this 30 ms delay Core 1 is sleeping. `Watchdog::feed()` is only called at the top of `loop()` (main.cpp:1619); `vTaskDelay` yields Core 1 and does NOT reset the TWDT subscription. The TWDT timeout is 5 s, so a single 30 ms delay is not dangerous. However, `startSession()` also calls `_evictOldSessions()` which contains an unbounded while-loop iterating over LittleFS directory entries and potentially removing multiple session files. The combination of directory enumeration + file removes + 30 ms delay could exceed expected latency if many session files accumulate (approaching the 150 KB free threshold).
- **Trigger:** Engine is started/stopped many times in a single bench session, filling flash. On the nth start, `_evictOldSessions()` iterates and deletes multiple files before the 30 ms delay.
- **Impact:** Control loop tick delayed; sensors not read; actuator commands not issued during the stall. For a 30 ms tick miss in STARTUP the impact is moderate (timing-sensitive blocks like StarterSpin could observe stale N1). The TWDT is not directly at risk from a single 30 ms call, but stacked calls (multiple rapid start/stop cycles) could accumulate.
- **Evidence:** `SessionLogger.cpp:116`, `SessionLogger.cpp:168` -- `vTaskDelay(30)`. `SessionLogger.cpp:113-158` -- `startSession` runs LittleFS open, write, flush; preceded by `_evictOldSessions`.
- **Suggested fix:** Remove or replace the `vTaskDelay` with a flag that signals Core 0 to finalize and flush. Core 1 should not block on Core 0 file I/O. `drainQueue()` is already designed for Core 0; let `endSession()` simply set `_open = false` and post a "close file" message to Core 0 via a flag or dedicated queue item.
- **Confidence:** High

---

### ECU-RTOS-05: RCInput ISRs read EngineData volatile floats via _updateCh then write to ed.idleInputRaw / ed.throttleInputRaw without FPU state save

- **Severity:** High
- **Bug class:** ISR safety / FPU context
- **Location:** `src/hal/RCInput.h:87-122`
- **Description:** The ISRs `_isrThr()` and `_isrIdle()` are correctly marked `IRAM_ATTR` and do not directly touch EngineData. However, `_updateCh()` (called from `RCInput::tick()` on Core 1 in the normal loop context) performs floating-point arithmetic: `float n = (float)((int)ch.pulseUs - Config::rcMinUs) / (float)range;` and `constrain(n, 0.0f, 1.0f)`. This is fine in loop context. The ISRs themselves read `_thr.pin` via `digitalRead()` -- `digitalRead` in Arduino-ESP32/IDF5 is documented as ISR-safe. No float math occurs inside the ISR body (`_isrThr`, `_isrIdle`). **Confirmed safe on this narrow point.** However: `ch.fresh` (a non-atomic `volatile bool`) and `ch.pulseUs` (a `volatile uint32_t`) are written by the ISR and read by `_updateCh` on Core 1 without a critical section. On Xtensa (single 32-bit load/store atomic), `uint32_t` reads are tear-free but the pair (`pulseUs` + `fresh`) is not protected as a unit: `_updateCh` reads `ch.fresh`, clears it, then reads `ch.pulseUs`. A CHANGE interrupt firing between those two reads on the same pin would overwrite `ch.pulseUs` and `ch.riseUs` in the ISR, but `ch.fresh` has already been cleared so the new pulse is silently dropped. This is a data-loss edge case, not a safety risk, because `valid` is held true from the previous pulse. Confidence is medium because the window is extremely narrow.
- **Trigger:** CHANGE ISR fires exactly between `ch.fresh = false` and `ch.pulseUs` read in `_updateCh`.
- **Impact:** One RC pulse silently dropped; `norm` retains previous value. Not safety-critical because failsafe timeout is checked separately.
- **Evidence:** `RCInput.h:110-116` -- `_updateCh` clears `fresh` then reads `pulseUs` without critical section. ISR at line 92 writes `pulseUs` then sets `fresh`.
- **Suggested fix:** Read `pulseUs` into a local variable before clearing `fresh` (i.e., read-copy-clear, not clear-then-read). Or use a portENTER_CRITICAL_ISR around the pair in `_updateCh`.
- **Confidence:** Medium

---

### ECU-RTOS-06: xTaskCreatePinnedToCore return value unchecked -- silent heap exhaustion leaves webTask never spawned

- **Severity:** High
- **Bug class:** Error handling / resource exhaustion
- **Location:** `src/main.cpp:1598`
- **Description:** The return value of `xTaskCreatePinnedToCore(webTask, "web", 12288, nullptr, 8, nullptr, 0)` is discarded. If heap is insufficient at boot (e.g., after a large ArduinoJson allocation elsewhere in setup(), or on a device with fragmented heap), the call returns `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY` and webTask is never created. Execution continues silently into `Watchdog::begin()` and then `loop()`. The engine runs normally but all web telemetry, remote commands, and flight log access are permanently unavailable, with no indication to the operator. Critically, the `SessionLogger::drainQueue()` is only called from `WebServer::tick()` inside webTask. If webTask was not created, the row queue (depth 20) fills after 10 seconds at the default 500 ms log rate, and all subsequent session rows are silently dropped.
- **Trigger:** Heap fragmentation at boot on a device that has previously allocated large ArduinoJson/WiFi buffers, or physical low-RAM variant.
- **Impact:** Operator has no web interface and no runtime visibility into engine state. Silent data loss.
- **Evidence:** `main.cpp:1598` -- return value cast to void. `SessionLogger.cpp:216` -- comment "Non-blocking -- drops silently if Core 0 is behind".
- **Suggested fix:** `TaskHandle_t webHandle = nullptr; if (xTaskCreatePinnedToCore(..., &webHandle, 0) != pdPASS) { Serial.println("[OT] FATAL: webTask alloc failed"); /* set fault flag or halt */ }`. At minimum, log the failure so the serial monitor reveals the problem.
- **Confidence:** High

---

### ECU-RTOS-07: CommandQueue::begin() xQueueCreate return value unchecked -- NULL queue causes silent command loss

- **Severity:** High
- **Bug class:** Error handling
- **Location:** `src/system/CommandQueue.h:63-65`
- **Description:** `CommandQueue::begin()` calls `xQueueCreate(QUEUE_DEPTH, sizeof(OTPacket))` and assigns the result directly to `_queue` without checking for NULL. `push()` checks `if (!_queue) return false`, so commands are silently dropped rather than crashing. Under heap exhaustion at boot, the queue handle is NULL and all commands (including STOP) are silently dropped for the entire session. The comment on `push()` says it is non-blocking and drops-if-full, giving the impression this is a handled graceful case, but a NULL handle means even a single STOP command never reaches Core 1.
- **Trigger:** OOM at boot; `CommandQueue::begin()` called after large allocations.
- **Impact:** STOP, FAULT_SHUTDOWN, and all other commands from web UI permanently unavailable. Engine cannot be commanded to stop.
- **Evidence:** `CommandQueue.h:64` -- unchecked `xQueueCreate`. `CommandQueue.h:69` -- `if (!_queue) return false` silently drops.
- **Suggested fix:** Assert or log if `xQueueCreate` returns NULL. Consider a `Serial.println("[FATAL] CommandQueue alloc failed")` followed by a deliberate halt in `begin()`.
- **Confidence:** High

---

### ECU-RTOS-08: FlightRecorder::lockLog() called from async_tcp task (not webTask) while _append uses 8 ms mutex timeout on Core 1 -- priority inversion risk

- **Severity:** Medium
- **Bug class:** Concurrency / priority inversion
- **Location:** `src/system/web/WebServer.cpp:559,604`, `src/system/FlightRecorder.cpp:292`
- **Description:** The `/api/log` and `/api/log/csv` HTTP GET handlers call `FlightRecorder::lockLog()` which takes `_mutex` with `portMAX_DELAY` (FlightRecorder.cpp:244). These handlers are registered via `_server.on()` and execute in the **async_tcp task** context (priority 10 on Core 0), not in webTask (priority 8). The async_tcp task runs at higher priority than webTask. Meanwhile, `FlightRecorder::_append()` on Core 1 takes the same mutex with a hard 8 ms timeout (FlightRecorder.cpp:292) to avoid stalling the ECU loop. If the async_tcp task holds the mutex for a full log-file read (potentially 250 ms on a large log per the code comment at line 291), Core 1's 8 ms attempts will time out and flight-critical log events (FAULT, BLOCK_EXIT) will be silently dropped. This is the intended behavior per the comment, but the risk is that safety-critical `logFault()` calls may fail to persist.
- **Trigger:** Browser fetches `/api/log` during active engine operation with a large log file.
- **Impact:** FAULT and RUNNING_ENTRY log records dropped. Post-incident analysis is compromised. No safety consequence during the session, but loss of black-box data.
- **Evidence:** `WebServer.cpp:559` -- `lockLog()` called in async_tcp HTTP handler. `FlightRecorder.cpp:244` -- `lockLog` uses `portMAX_DELAY`. `FlightRecorder.cpp:291-292` -- comment says "toJson() on Core 0 can hold the mutex for ~250 ms".
- **Suggested fix:** Move log-read handlers to execute under webTask (not async_tcp) using AsyncResponseStream with chunked reads and releases of the mutex between chunks. Alternatively, restrict `lockLog()` to only be called from webTask context and use a non-blocking approach in async_tcp.
- **Confidence:** High

---

### ECU-RTOS-09: EngineData non-volatile char arrays (lastEvent, faultDescription, currentBlock, seqWaitReason) written on Core 1, read on Core 0 without synchronization -- torn string reads possible

- **Severity:** Medium
- **Bug class:** Concurrency / data race
- **Location:** `src/engine/EngineData.h:128,133,150,153`
- **Description:** `EngineData` documents that individual 32-bit aligned scalar reads are tear-free on Xtensa. However, `lastEvent[64]`, `faultDescription[192]`, `currentBlock[32]`, and `seqWaitReason[80]` are plain (non-volatile) `char` arrays. They are written by Core 1 via `strncpy`/`snprintf` (multi-byte store sequences) and read by Core 0 inside `_buildTelemetry()` for WebSocket transmission. A `strncpy` of 64 bytes is not a single atomic operation; Core 0 can observe the string partially written. In most cases this produces a garbled human-readable string, not an unsafe action. However, `faultDescription` is used in the fault banner shown to the operator and `lastEvent` drives the UI state display -- a torn read that produces a null-like string clears the fault banner, potentially hiding an active fault from the operator.
- **Trigger:** Core 1 writes `faultDescription` during a fault shutdown; Core 0 is simultaneously building a WebSocket telemetry frame.
- **Impact:** Operator sees blank fault description or a mixed old/new string for one WebSocket tick. Not safety-critical (engine already in fault state) but could mislead operator during diagnosis.
- **Evidence:** `EngineData.h:128` -- `char lastEvent[64]` (not volatile). `main.cpp:1054` -- `snprintf(ed.lastEvent, ...)`. `EngineData.h` comment at line 28 explicitly covers only scalar 32-bit fields.
- **Suggested fix:** For human-readable strings, a double-buffer scheme or a sequence counter is overkill. Minimum: add a `volatile` qualifier and document that readers may see partial strings. Better: copy strings under a short critical section or keep a `volatile uint8_t stringGen` counter that Core 0 checks before and after copying.
- **Confidence:** Medium

---

### ECU-RTOS-10: SessionLogger _file handle accessed from both Core 1 (endSession drainQueue) and Core 0 (drainQueue in webTask tick) without synchronization

- **Severity:** Medium
- **Bug class:** Concurrency / data race
- **Location:** `src/system/SessionLogger.cpp:162-178`, `src/system/SessionLogger.cpp:220-230`
- **Description:** `endSession()` sets `_open = false`, then calls `vTaskDelay(30)` to "let Core 0 finish any in-progress drain", then calls `_writeRow()` directly using `_file` (line 173). Simultaneously, if Core 0 is mid-execution inside `drainQueue()` and its `while(_open && ...)` loop just evaluated `_open` as true before Core 1 set it to false, Core 0 will continue calling `_writeRow()` on `_file` concurrently with Core 1. Both tasks write to the same `File` object. The 30 ms `vTaskDelay` is a heuristic, not a synchronization guarantee. If webTask is delayed by DNS, eviction, or a long `Config::flushPendingSave()` call, it may still be inside `drainQueue()` when Core 1 resumes.
- **Trigger:** Engine shutdown (enterStandby) while Core 0 is in the middle of a `drainQueue()` call that is holding `_file` open.
- **Impact:** LittleFS file handle corruption; session CSV truncated or containing garbled rows. Session log data loss.
- **Evidence:** `SessionLogger.cpp:167-173` -- `_open=false`, delay(30), then `_writeRow(_file)`. `SessionLogger.cpp:224` -- `while(_open && xQueueReceive(...))` uses volatile `_open` but the check-and-use is not atomic.
- **Suggested fix:** Use a FreeRTOS mutex or binary semaphore to gate `_file` access. `endSession()` takes the lock, signals Core 0 to stop, drains remaining queue items, then closes. Alternatively, let Core 0 exclusively own `_file` and close it on receiving a sentinel queue item from Core 1.
- **Confidence:** High

---

### ECU-RTOS-11: CONFIG_ASYNC_TCP_USE_WDT=0 disables async_tcp watchdog subscription but allows async_tcp task to stall indefinitely on Core 0 without any detection

- **Severity:** Medium
- **Bug class:** Watchdog / fail-safe
- **Location:** `platformio.ini:28`, `src/system/Watchdog.h`
- **Description:** `CONFIG_ASYNC_TCP_USE_WDT=0` is set in platformio.ini specifically because AsyncTCP 3.x subscribes itself to TWDT and blocks on `portMAX_DELAY`, causing WDT reboots. The workaround disables the AsyncTCP WDT subscription. The consequence is that the async_tcp task (priority 10, Core 0) can now hang indefinitely without triggering a reset. The only Core 0 task with any form of timeout is webTask (priority 8), which calls `WebServer::tick()` every 5-20 ms. However, if async_tcp itself hangs (e.g., waiting on TCP ACK with an infinite timeout), webTask may still run but all HTTP/WebSocket handling will stall because async_tcp's internal event queue is blocked. Core 1 is unaffected by Core 0 hangs. The engine continues to operate but the operator loses all web telemetry and command capability.
- **Trigger:** Network-side TCP stall (client disappears mid-transfer), or LittleFS FS mutex contention inside async_tcp context (documented concern at WebServer.cpp:49).
- **Impact:** Silent loss of operator command interface. STOP command via web UI is permanently unavailable until reboot. Hardware stop switch is still functional.
- **Evidence:** `platformio.ini:28` -- `CONFIG_ASYNC_TCP_USE_WDT=0`. `WebServer.cpp:1054` -- code comment acknowledges this trade-off.
- **Suggested fix:** Subscribe webTask (Core 0) to TWDT separately from async_tcp using `esp_task_wdt_add()`, and feed it in `WebServer::tick()`. This catches webTask hangs without re-enabling AsyncTCP's problematic TWDT behavior. The 5 s timeout is appropriate.
- **Confidence:** High

---

### ECU-RTOS-12: CommandQueue depth 16 insufficient under captive-portal command burst -- STOP command can be dropped

- **Severity:** Medium
- **Bug class:** Queue overflow / safety
- **Location:** `src/system/CommandQueue.h:61`
- **Description:** The command queue depth is 16. The web UI can submit rapid sequences of commands (particularly test commands: OIL_SCAV_TEST, COOL_FAN_TEST, AIRSTARTER_TEST, etc.). `push()` is non-blocking and silently returns false on queue full. If the queue fills with test or diagnostic commands while a STOP command is also pushed (e.g., from a JavaScript auto-submit on navigation away), the STOP command is dropped. The comment on `push()` says "drops if full" which is correct for non-critical commands but STOP is safety-relevant. Additionally, the captive portal DNS resolver fires many requests on first connection which can generate multiple `/api/data` GET requests that each push no commands, but error-handling in a malformed POST could loop-push identical commands.
- **Trigger:** Operator opens captive portal page while test commands are being submitted, or JavaScript error causes rapid command re-submission.
- **Impact:** STOP or FAULT_SHUTDOWN commands dropped during a queue-full condition. Engine continues when operator expects shutdown.
- **Evidence:** `CommandQueue.h:61,70` -- `QUEUE_DEPTH=16`, non-blocking push. No priority or command-class distinction in the queue.
- **Suggested fix:** Reserve a dedicated slot for STOP/FAULT commands: check if the incoming command is STOP or FUEL_SOL_TEST and if the queue is full, drain one item before inserting. Alternatively, increase queue depth to 32 and add a high-priority STOP path that bypasses the queue by writing directly to EngineData (safe since Core 0 writes to EngineData only via this queue).
- **Confidence:** Medium

---

### ECU-RTOS-13: webTask priority (8) higher than ECU loopTask (1) on same-core basis -- priority inversion via FlightRecorder mutex contention on Core 0

- **Severity:** Medium
- **Bug class:** Priority inversion
- **Location:** `src/main.cpp:1598`, `src/system/FlightRecorder.cpp:257`
- **Description:** On ESP32 with two cores, Core 0 runs webTask (prio 8) and async_tcp (prio 10). Core 1 runs the Arduino loopTask (prio 1). FreeRTOS on ESP32 does not do cross-core priority inheritance. `FlightRecorder::runEviction()` runs on Core 0 (webTask) and takes `_mutex` with `portMAX_DELAY`. The ECU loop on Core 1 calls `_append()` with `pdMS_TO_TICKS(8)`. If `runEviction()` holds the mutex for 200 ms (the documented worst case), Core 1 times out and the append is dropped -- this is the intended behavior. The inversion risk is the reverse: if Core 1 holds the mutex in `_append()` (up to the LittleFS open + write + close time), Core 0 webTask is blocked in `runEviction()`. Since the webTask has `drainQueue()` after `runEviction()` in tick(), session rows also accumulate during the block. LittleFS write times on ESP32 with a 4 MB flash can be 5-50 ms per file open. The combination can cause webTask to miss its 5 ms / 20 ms loop period for one tick, causing WebSocket frame delay.
- **Trigger:** LittleFS write latency spike (worn flash, power-marginal supply) while Core 0 is inside `runEviction` and Core 1 is inside `_append`.
- **Impact:** WebSocket frame delayed; SessionLogger drainQueue stalls; not an engine-safety issue.
- **Evidence:** `FlightRecorder.cpp:257` -- `runEviction` portMAX_DELAY. `FlightRecorder.cpp:292` -- `_append` 8 ms timeout. `WebServer.cpp:1113-1115` -- tick calls runEviction then drainQueue sequentially.
- **Suggested fix:** Limit `runEviction()` mutex hold time by performing the file-copy work in bounded chunks across multiple tick calls (state machine). This also reduces the probability of the 8 ms Core 1 timeout.
- **Confidence:** Medium

---

### ECU-RTOS-14: RCInput::begin() uses compile-time OT_IDLE_INPUT_PIN / OT_THROTTLE_INPUT_PIN constants -- no runtime pin validity check before attachInterrupt

- **Severity:** Low
- **Bug class:** ISR safety / hygiene
- **Location:** `src/hal/RCInput.h:37-49`
- **Description:** `OT_IDLE_INPUT_PIN` and `OT_THROTTLE_INPUT_PIN` are compile-time constants (`#define` in `hardware_profile.h`). `digitalPinToInterrupt()` on ESP32 returns the same value for valid GPIO or -1 for restricted pins (USB D+/D-, strapping pins). There is no check: if a pin is mapped to a restricted GPIO, `attachInterrupt(-1, ...)` on ESP32-Arduino is a no-op but does not log an error. The ISR is never attached, the `Ch.pin` is set, and `_updateCh` will timeout on `rcFailsafeMs` and set `valid=false`, silently degrading to no RC input. No operator notification is generated.
- **Trigger:** Hardware profile defines an RC PWM pin that is a restricted ESP32-S3 GPIO (e.g., GPIO 19/20 = USB).
- **Impact:** RC input silently non-functional. Failsafe timeout eventually sets `valid=false`, which is the correct safe state.
- **Evidence:** `RCInput.h:39,46` -- `attachInterrupt(digitalPinToInterrupt(_idle.pin), ...)` with no return-value check or pin validation.
- **Suggested fix:** Add `if (digitalPinToInterrupt(_idle.pin) == NOT_AN_INTERRUPT) { Serial.printf("[RCInput] ERROR: GPIO%d cannot be used as interrupt\n", _idle.pin); return; }` before `attachInterrupt`.
- **Confidence:** High

---

### ECU-RTOS-15: EngineData::extraCooldownUntilMs is volatile unsigned long -- on 64-bit targets this could be a torn read; on ESP32 (32-bit) it is safe but the type is wider than uint32_t on some compilers

- **Severity:** Low
- **Bug class:** Portability / hygiene
- **Location:** `src/engine/EngineData.h:178`
- **Description:** `volatile unsigned long extraCooldownUntilMs = 0` stores an absolute `millis()` deadline. On ESP32/Xtensa `unsigned long` is 32 bits, same as `uint32_t`, so a single load is atomic. If this firmware is ever compiled for ESP32-S3 with a 64-bit toolchain variant, or if a future Xtensa ISA update changes alignment, the assumption breaks. More concretely, `millis()` returns `unsigned long` which is 32-bit and wraps at ~49 days. The field is only used for a cooldown deadline so wrap is benign. The inconsistency (other deadline fields use `uint32_t` or `unsigned long`) is hygiene-level.
- **Trigger:** Portability: not a current bug on ESP32.
- **Impact:** None on current target.
- **Evidence:** `EngineData.h:178` -- `volatile unsigned long` vs other volatile fields using `uint32_t`.
- **Suggested fix:** Change to `volatile uint32_t extraCooldownUntilMs` for consistency with other timing fields.
- **Confidence:** High (correct on current target, low portability risk)

---

## Notes / unclear areas

1. **Config::fromJson called directly from Core 0 async_tcp context (POST /api/config, WebServer.cpp:485) writes to Config static fields.** Core 1 reads `Config::*` fields inside `_append`, block onEnter, and controller tick. Config statics are plain `static` variables (no `volatile`, no mutex). Individual 32-bit reads are atomic on Xtensa, but a POST that updates many Config fields (e.g., all PID tuning params in one JSON) presents a multi-write window. The APPLY_CONFIG command queue path (line 538) pushes an APPLY_CONFIG to Core 1, which calls `Hardware::applyConfig()` to propagate values to block instances -- but the underlying `Config::*` statics are already mutated on Core 0 before that command is processed. This is acknowledged as accepted behavior per the comment at WebServer.cpp:540 ("Config values are live in memory immediately"), but deserves a severity review if any Config field controls a safety threshold that is read directly by `SafetyMonitor` on Core 1.

2. **webTask stack comment mentions "char buf[5120]" (main.cpp:1594) but no such local array was found** -- the 7168-byte buffer at WebServer.cpp:1093 is `static`, which is correct. The 8 KB + 8 KB `g_webRxBuf` / `g_webTxBuf` are also `static`. The 12 KB webTask stack appears adequate for the current call patterns, though there is no `uxTaskGetStackHighWaterMark()` monitoring in the codebase to confirm this at runtime.

3. **FlightRecorder mutex `_mutex` is checked for NULL before every use**, but `begin()` creates it with `if (!_mutex) _mutex = xSemaphoreCreateMutex()`. If `begin()` is called before the FreeRTOS scheduler is running (possible if called from pre-scheduler `setup()` before any task creation), `xSemaphoreCreateMutex()` will succeed. This is safe on ESP32-Arduino because the scheduler is started before `setup()` is called. No issue, but worth documenting.

4. **CLEAR_LOG command is processed on Core 1** (main.cpp:1295-1296) and calls `FlightRecorder::clear()` which deletes the log file. If Core 0 is simultaneously reading the log file via `lockLog()` / `toJson()`, and Core 1 removes it under the mutex, the already-open `File` handle on Core 0 may become invalid. LittleFS on ESP32 keeps an open-file reference count and the remove may fail or succeed depending on LittleFS version. The mutex prevents concurrent clear+read, but if `lockLog` is called from async_tcp context (not holding the mutex yet) and `clear()` wins the mutex first, the subsequent `LittleFS.open()` in the HTTP handler will return an invalid handle. This is handled gracefully by the `if (!f)` check at WebServer.cpp:566.

5. **`_savePending` volatile bool (Config.cpp:332)** written on Core 1 (`Config::requestSave()`) and read/cleared on Core 0 (`Config::flushPendingSave()` in WebServer::tick). On Xtensa a single bool store/load is atomic. The check-and-clear in `flushPendingSave()` (lines 341-342: `if (!_savePending) return false; _savePending = false;`) is not atomic as a pair, but the only consequence of a TOCTOU race here is a spurious extra save call, which is harmless.
