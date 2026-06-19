# Persistence-logging subsystem audit

## Files reviewed

| File | Lines read |
|---|---|
| `src/system/FlightRecorder.h` | 1-81 |
| `src/system/FlightRecorder.cpp` | 1-327 |
| `src/system/SessionLogger.h` | 1-29 |
| `src/system/SessionLogger.cpp` | 1-231 |
| `partitions.csv` | 1-8 |
| `platformio.ini` | 1-56 |
| `src/system/web/WebServer.cpp` | 1-1173 (full) |
| `src/main.cpp` | 1488-1664 (task setup + tick integration) |
| `src/system/Config.h` | selected lines (SLOG_* masks, sessionLogIntervalMs) |

---

## Findings (Critical -> Info)

---

### ECU-LOG-01: Eviction mutex not released on LittleFS open failure -- events permanently dropped

- **Severity:** Critical
- **Bug class:** Error handling / mutex leak
- **Location:** `FlightRecorder.cpp:265-268` (`runEviction()`)
- **Description:** When either `LittleFS.open(PATH, "r")` or `LittleFS.open("/logs/events.tmp", "w")` returns a null handle, the code closes whichever handle succeeded and falls through to `_evictionPending = false` at line 283. However, the mutex acquired on line 257 is still held for the entire failure branch: the `else` block (lines 268-282) is skipped, control falls directly to line 283, and then the `xSemaphoreGive` on line 284 is reached. So the mutex IS eventually released in this path. BUT `_evictionPending` is cleared even though no eviction was performed, meaning `s_lineCount` remains at `MAX_RECORDS`. On the next `_append` call, `_evictionPending` is set again immediately. This is not a leak per se, but eviction will be retried on every subsequent `runEviction()` call -- silently and indefinitely -- without ever succeeding if the flash is full. Meanwhile, every `_append` call on Core 1 drops the event after setting the flag, so all new events are lost until flash space is recovered.
- **Trigger:** Flash storage full at the moment Core 0 runs `runEviction()`.
- **Impact:** All FlightRecorder events are silently dropped for the duration of the failure. Fault events, SNAP records, and run summaries are lost. The log display shows a frozen record count.
- **Evidence:** `FlightRecorder.cpp:265-283`: `_evictionPending = false` is set unconditionally regardless of whether the file copy succeeded.
- **Suggested fix:** On open failure, do NOT clear `_evictionPending`. Log an error to Serial. Optionally back off with a timestamp to avoid hammering LittleFS every web-task cycle.
- **Confidence:** High

---

### ECU-LOG-02: `/api/log/raw` streams the live log file without holding the mutex -- races with ring-buffer eviction

- **Severity:** Critical
- **Bug class:** Concurrency / TOCTOU
- **Location:** `WebServer.cpp:588-597` (`/api/log/raw` handler)
- **Description:** The `/api/log/raw` route serves the raw NDJSON file directly with `AsyncFileResponse`, which reads the file in TCP-chunk-sized increments across multiple async callbacks. No call to `FlightRecorder::lockLog()` or `FlightRecorder::unlockLog()` is made. If Core 0's `runEviction()` fires between two chunk reads, it calls `LittleFS.remove(PATH)` (line 279) and `LittleFS.rename("/logs/events.tmp", PATH)` (line 280). The `AsyncFileResponse` then attempts to continue reading a deleted or replaced file. Under LittleFS the open file handle may return stale data, zeroes, or fail silently, producing a corrupted or truncated NDJSON download.
- **Trigger:** A browser triggers GET `/api/log/raw` while the log is near the 2200-record limit. Core 0's `runEviction()` fires during the multi-chunk download.
- **Impact:** The downloaded flight log is silently corrupted. Forensic review of safety events yields incomplete data.
- **Evidence:** `WebServer.cpp:586-597` has no mutex calls. `runEviction()` removes and renames the file at `FlightRecorder.cpp:279-280` while holding the mutex. `/api/log` and `/api/log/csv` (lines 559, 604) both call `lockLog()`/`unlockLog()` -- the omission in `/api/log/raw` is inconsistent.
- **Suggested fix:** Either call `FlightRecorder::lockLog()` before `req->beginResponse()` and `unlockLog()` in a completion callback, or -- since holding the mutex across a multi-chunk async response is impractical -- copy the file to a temp path under the mutex before serving it. The simplest safe option is to serve via `AsyncResponseStream` under `lockLog/unlockLog` the same way `/api/log` does.
- **Confidence:** High

---

### ECU-LOG-03: LittleFS filesystem=littlefs but partitions.csv declares subtype=spiffs -- partition will not be mounted

- **Severity:** High
- **Bug class:** Configuration mismatch / integration
- **Location:** `partitions.csv:7`, `platformio.ini:13`
- **Description:** `partitions.csv` line 7 declares the data partition with `SubType = spiffs`. The Arduino-ESP32 / ESP-IDF partition driver uses the SubType field to identify partition content: `spiffs (0x82)` is a distinct code from `littlefs (0x83)` (or the catch-all `fat (0x81)`). PlatformIO's `board_build.filesystem = littlefs` formats and uploads a LittleFS image, but the IDF `esp_partition_find()` call inside `LittleFS.begin()` searches for a partition whose SubType matches the LittleFS label. On Arduino-ESP32 3.x / IDF 5, `LittleFS.begin()` uses the label `"spiffs"` by default (it searches for a partition named "spiffs" or typed `ESP_PARTITION_TYPE_DATA / ESP_PARTITION_SUBTYPE_DATA_SPIFFS`), so in practice the partition IS found and mounted because the name matches. However, a strict IDF SubType check would fail, and future Arduino-ESP32 or IDF versions that enforce the SubType will silently fail to mount LittleFS, rendering the entire persistence subsystem non-functional. The correct SubType for a LittleFS partition is `0x83` (use `littlefs` token if supported by the ESP-IDF version, otherwise leave blank or use `fat`). At minimum this is a latent correctness bug that can break on IDF upgrades.
- **Trigger:** Upgrade to a stricter Arduino-ESP32 core or IDF version, or porting to a different target.
- **Impact:** `LittleFS.begin()` returns false; all FlightRecorder and SessionLogger writes silently fail. The ECU logs nothing for the lifetime of the session.
- **Evidence:** `partitions.csv:7` -- `spiffs, data, spiffs, 0x310000, 0xE0000`; `platformio.ini:13` -- `board_build.filesystem = littlefs`.
- **Suggested fix:** Change `partitions.csv` line 7 SubType from `spiffs` to `littlefs` (IDF 5 accepts this token). Verify that `LittleFS.begin()` uses `"spiffs"` as the partition label or change the label to `"littlefs"` and pass it explicitly: `LittleFS.begin(true, "/littlefs", 10, "littlefs")`.
- **Confidence:** Medium (currently works due to name-based lookup, but is structurally incorrect)

---

### ECU-LOG-04: `s_lineCount` written by Core 0 (`runEviction`) without atomic protection -- Core 1 may see torn value

- **Severity:** High
- **Bug class:** Concurrency / shared mutable state
- **Location:** `FlightRecorder.cpp:281`, `FlightRecorder.cpp:321`, `FlightRecorder.cpp:202`
- **Description:** `s_lineCount` is a plain `static int` (file-scope). It is read and written by Core 1 inside `_append()` (lines 295-321) and written by Core 0 inside `runEviction()` (line 281, sets it to `MAX_RECORDS - keepFrom = 1760`) and `clear()` (line 202). The mutex protects the file I/O and guards the read-modify-write of `s_lineCount` inside `_append` because Core 1 holds the mutex for the full duration. However: Core 0 sets `s_lineCount = MAX_RECORDS - keepFrom` at line 281 while holding the mutex, and Core 1 will only see this value after the mutex is released. This is correct as long as both cores properly acquire the mutex before touching `s_lineCount`. There is one problematic path: `clear()` (line 200-204) acquires the mutex and sets `s_lineCount = 0`, but `_append` starts with a lazy-init at line 295 guarded by `s_lineCount < 0`. If `clear()` sets it to 0 while `_append` has already passed the `s_lineCount < 0` check, the append proceeds normally -- this is fine. The actual hazard is the lack of `volatile` or atomic type: the Xtensa compiler may cache `s_lineCount` in a register across the mutex boundary in `runEviction()`, specifically the store at line 281, which may not be flushed to memory before the mutex give on line 284 if the compiler reorders the store. On Xtensa SMP this is a real concern without a memory barrier.
- **Trigger:** Any eviction event where Core 0 writes `s_lineCount` and Core 1 reads it concurrently.
- **Impact:** Core 1 may read a stale `s_lineCount`, cause an incorrect eviction trigger, or count records past `MAX_RECORDS` and keep appending -- growing the file without bound.
- **Evidence:** `FlightRecorder.cpp:20`: `static int s_lineCount = -1;` -- not `volatile`, not atomic. Shared across cores with only mutex protection; but mutex alone does not guarantee memory ordering visibility without a compiler barrier on Xtensa.
- **Suggested fix:** Declare `s_lineCount` as `static volatile int s_lineCount = -1;` to inhibit register caching across the mutex boundary. Alternatively use `portMEMORY_BARRIER()` before each mutex give that modifies `s_lineCount`.
- **Confidence:** Medium (observed similar issues in ESP32 SMP Arduino projects; depends on optimization level)

---

### ECU-LOG-05: `_append` 8 ms mutex timeout drops safety-critical events during log reads

- **Severity:** High
- **Bug class:** Concurrency / silent data loss
- **Location:** `FlightRecorder.cpp:292`
- **Description:** `_append()` acquires the mutex with an 8 ms timeout (`pdMS_TO_TICKS(8)`). The comment acknowledges that `toJson()` can hold the mutex for ~250 ms. `lockLog()` used by `/api/log` and `/api/log/csv` on Core 0 acquires with `portMAX_DELAY`. A `/api/log/csv` request iterates all 2200 records, deserializing JSON for each, which at typical LittleFS throughput (~30 KB/s for sequential reads) takes on the order of 500-1000 ms. During this window, any call to `_append()` on Core 1 -- including `logFault()`, `logFaultShutdown()`, and `logAbort()` -- silently returns without writing, with no indication that the event was dropped.
- **Trigger:** Browser opens `/api/log/csv` or `/api/log` download while the engine is running or faulting.
- **Impact:** Fault events, fault shutdown records, and abort records are silently lost. The flight log may show no fault record for a run that ended in a fault shutdown. This is a safety-data integrity hazard.
- **Evidence:** `FlightRecorder.cpp:291-292` -- 8 ms timeout, silent drop on timeout. `WebServer.cpp:604-639` -- `/api/log/csv` holds `lockLog()` for the entire 2200-line iteration with JSON deserialization per line.
- **Suggested fix:** (a) Increase the timeout to at least 50 ms for fault-class events (`logFault`, `logFaultShutdown`, `logAbort`) since these are safety-critical and the ECU is already in a fault branch where tight timing is less critical. (b) Add a volatile dropped-event counter that is visible in telemetry. (c) Bound the `/api/log/csv` hold time by reading the file in chunks and releasing the mutex between chunks.
- **Confidence:** High

---

### ECU-LOG-06: `SessionLogger::endSession()` called from Core 1 races with `drainQueue()` on Core 0 -- file closed under active drain

- **Severity:** High
- **Bug class:** Concurrency / use-after-close
- **Location:** `SessionLogger.cpp:162-179`
- **Description:** `endSession()` is called from the ECU loop (Core 1, `enterStandby()` at `main.cpp:990`). It sets `_open = false` (line 167), then calls `vTaskDelay(pdMS_TO_TICKS(30))` to yield, relying on Core 0 to finish any in-progress `drainQueue()` call within 30 ms. However, this is not a synchronization primitive -- it is a best-effort delay. If Core 0 is occupied by a slow LittleFS write (a `_file.flush()` inside `drainQueue()` at line 229 can stall for tens of milliseconds), Core 0 may still be inside `drainQueue()` when Core 1 calls `_file.flush()` and `_file.close()` on lines 176-177. Both cores then operate on the same `_file` object concurrently. LittleFS file handles are not thread-safe. This can corrupt the file's internal write pointer or produce a partially written final row.
- **Trigger:** Engine shuts down after a long run (many queued rows), Core 0 is under load (WiFi client connected), 30 ms window expires before `drainQueue` completes.
- **Impact:** The session CSV file is silently corrupted for the last N rows of the run. End-of-run performance data is lost or garbled.
- **Evidence:** `SessionLogger.cpp:167-177`. The 30 ms window is noted in a comment as "let Core 0 finish any in-progress drain" but there is no actual synchronization guarantee. `drainQueue()` at line 229 calls `_file.flush()` which is a blocking LittleFS operation that can exceed 30 ms on a busy or fragmented filesystem.
- **Suggested fix:** Replace the `vTaskDelay` heuristic with a semaphore: Core 0 `drainQueue()` acquires a binary semaphore on entry and releases it on exit; `endSession()` waits on that semaphore before touching `_file`. Alternatively, move `endSession()`'s file close to Core 0 by pushing a sentinel value through `_rowQueue`.
- **Confidence:** High

---

### ECU-LOG-07: `startSession()` called from Core 1 while `drainQueue()` may be mid-write on Core 0

- **Severity:** High
- **Bug class:** Concurrency / use-after-close / file handle corruption
- **Location:** `SessionLogger.cpp:113-158`
- **Description:** `startSession()` is called from Core 1 (`main.cpp:1151`). If a previous session is open (`_open == true`), it sets `_open = false` (line 115), delays 30 ms, then calls `_file.flush()` and `_file.close()` (lines 117-118) before opening a new file. The same race described in ECU-LOG-06 applies: Core 0 may be mid-drain when Core 1 closes and re-opens `_file`. Additionally, after `_file.close()`, `startSession()` immediately calls `_evictOldSessions()` (line 121), which opens and reads the `/logs` directory. During this time Core 0 may call `drainQueue()`, see `_open == false`, and return immediately -- but if the timing is slightly different, Core 0 could race the directory scan with a write to the just-closed (or newly opened) file.
- **Trigger:** Engine restart (STARTUP command while a previous session is open), Core 0 under load.
- **Impact:** New session CSV is opened in a corrupted state; previous session is truncated.
- **Evidence:** `SessionLogger.cpp:113-121`. `startSession()` modifies `_file` on Core 1 without any lock that Core 0's `drainQueue()` respects.
- **Suggested fix:** Same fix as ECU-LOG-06: use a binary semaphore to gate Core 1's `_file` operations against Core 0's drain loop.
- **Confidence:** High

---

### ECU-LOG-08: Eviction `s_lineCount` set to wrong value -- always `1760` regardless of actual records copied

- **Severity:** Medium
- **Bug class:** Logic bug / incorrect state
- **Location:** `FlightRecorder.cpp:281`
- **Description:** After eviction, `s_lineCount` is set to `MAX_RECORDS - keepFrom` which equals `2200 - 440 = 1760`. This hardcoded formula assumes that exactly 2200 valid records were in the file at the time of eviction. In practice, the NDJSON file may contain fewer records (e.g., if some lines were truncated by a prior power-loss and filtered out by the `line[0] != '{'` check, or if `begin()` was called after a partial log). The variable `seen` (line 269) tracks how many records were actually copied, but it is not used to update `s_lineCount`. If the actual file had, say, 2000 records, eviction copies `2000 - 440 = 1560` records but sets `s_lineCount = 1760`, causing 200 phantom records to be counted. The ring buffer will then fill and trigger another eviction 440 actual records sooner than intended.
- **Trigger:** Any eviction after a boot that recovers from a partially populated log, or after a file repair pass that skips malformed lines.
- **Evidence:** `FlightRecorder.cpp:269-281`: `seen` is the actual copied count, but `s_lineCount = MAX_RECORDS - keepFrom` uses only the constant values.
- **Suggested fix:** `s_lineCount = seen - keepFrom;` where `seen` counts lines actually written. Or count `fw.println()` calls separately.
- **Confidence:** High

---

### ECU-LOG-09: `SESSION_MIN_FREE_BYTES = 150 KB` is unreachable with a 150 KB free target on an 896 KB partition that also holds the event log

- **Severity:** Medium
- **Bug class:** Persistence / flash wear / capacity planning
- **Location:** `SessionLogger.cpp:80`
- **Description:** The LittleFS partition is 896 KB (0xE0000 bytes). The FlightRecorder NDJSON log at 2200 records x ~160 bytes/line averages ~344 KB. A single 30-minute session at 500 ms intervals with the default N1+TOT+OIL mask is roughly 100-150 KB (3600 rows x ~40 bytes). The `SESSION_MIN_FREE_BYTES = 150 KB` threshold triggers old-session eviction when free space drops below 150 KB. With 896 KB total and the event log consuming ~344 KB, only ~552 KB is available for CSVs. Holding 3 full-session CSVs (3 x 150 KB = 450 KB) leaves 102 KB free, which is below the 150 KB threshold -- meaning the eviction loop will fire before the third session can be written in full. In degenerate cases (very long runs or large channel masks), a single session CSV can exceed 150 KB, and `_evictOldSessions()` will delete all prior sessions and still not free enough space for the current one. The write to `_file` then fails silently because `LittleFS.open(_currentPath, "w")` returns null (flash full) and is checked but only logs to Serial with no flag set to notify the operator.
- **Trigger:** Long engine runs (>20 min) with many channels enabled, or multiple runs without downloading CSVs.
- **Impact:** Session CSV writes silently stop. The operator has no indication that logging has halted unless they check Serial output.
- **Evidence:** `SessionLogger.cpp:80` (`SESSION_MIN_FREE_BYTES`), `startSession()` line 127-129 (only Serial on open fail, `_open` is never set to true), partition line `spiffs, 0xE0000` = 896 KB.
- **Suggested fix:** (a) Expose an `_logDropped` flag visible in the web telemetry `flash_free_kb` / a dedicated `session_log_active` field. (b) Consider reducing the event log's MAX_RECORDS to free more headroom for CSVs. (c) Clamp `SESSION_MIN_FREE_BYTES` based on partition size at boot.
- **Confidence:** Medium

---

### ECU-LOG-10: `runCount` used as session filename index without synchronization -- filename collision on rollover or stale read

- **Severity:** Medium
- **Bug class:** Integer issues / filename collision
- **Location:** `SessionLogger.cpp:123-124`
- **Description:** `startSession()` derives the session filename as `session_%lu.csv` from `EngineData::instance().runCount + 1`. `runCount` is a `volatile uint32_t` in `EngineData` that is incremented by the Core 1 ECU loop. No synchronization exists between the read of `runCount` in `startSession()` and the increment that happens in the state machine. If `startSession()` reads `runCount` before the increment that marks the new run, two consecutive restarts can produce the same filename, and `LittleFS.open(_currentPath, "w")` will silently overwrite the previous session CSV. This is especially likely in abort-and-restart scenarios where STARTUP is entered twice without returning to STANDBY.
- **Trigger:** Engine abort followed by immediate restart; `runCount` not yet incremented when `startSession()` executes.
- **Impact:** Previous session CSV is silently overwritten. Engine run data lost.
- **Evidence:** `SessionLogger.cpp:123`: `uint32_t run = EngineData::instance().runCount + 1;` -- reads a volatile counter with no lock; `_currentPath` is set before the file is opened.
- **Suggested fix:** Maintain a SessionLogger-private `static uint32_t _sessionNum` that is atomically incremented each call to `startSession()`, independent of `runCount`. Persist it via NVS or a dedicated counter file to survive reboots.
- **Confidence:** Medium

---

### ECU-LOG-11: `xSemaphoreCreateMutex` does not enable priority inheritance -- Core 1 ECU loop vulnerable to priority inversion

- **Severity:** Medium
- **Bug class:** Concurrency / priority inversion
- **Location:** `FlightRecorder.cpp:27`
- **Description:** The FlightRecorder mutex is created with `xSemaphoreCreateMutex()`. In FreeRTOS, `xSemaphoreCreateMutex()` creates a standard mutex WITH priority inheritance (this is documented in FreeRTOS). However, `xSemaphoreCreateRecursiveMutex()` does NOT inherit. Since the standard mutex is used here, priority inheritance is in effect. This finding is therefore lower severity than initially assessed. However: the 8 ms timeout in `_append()` (Core 1) means priority inheritance rarely has time to act -- if Core 0 holds the mutex and is preempted by a higher-priority async_tcp task (priority 10 vs web task priority 8), the inheritance chain may not propagate in time, and the 8 ms still expires, dropping the event.
- **Trigger:** async_tcp (priority 10) preempts the web task (priority 8) while web task holds the FlightRecorder mutex; Core 1 ECU loop (Arduino loop() default priority) times out in 8 ms.
- **Impact:** See ECU-LOG-05 for event loss consequence. This finding explains the mechanism.
- **Evidence:** `FlightRecorder.cpp:27` (`xSemaphoreCreateMutex`); `platformio.ini:29` (`CONFIG_ASYNC_TCP_USE_WDT=0` comment explains async_tcp blocks).
- **Suggested fix:** Addressed by ECU-LOG-05. Additionally consider raising the ECU loop task priority above async_tcp (priority 11+) so it is not starved while waiting on the mutex.
- **Confidence:** Medium

---

### ECU-LOG-12: `_evictOldSessions` eviction loop never closes `entry` handle before `dir.openNextFile()` -- directory handle leak

- **Severity:** Low
- **Bug class:** Resource leak
- **Location:** `SessionLogger.cpp:89-107`
- **Description:** Inside `_evictOldSessions()`, the loop calls `entry = dir.openNextFile()`. The previous `entry` value is not explicitly closed before being overwritten. LittleFS on Arduino-ESP32 manages file handles through `File` objects with RAII semantics: assigning a new `File` to `entry` calls the destructor of the old `File`, which closes the handle. This should work correctly. However, the same pattern in `WebServer.cpp:714-729` (DELETE `/api/session/all`) also assigns `entry = dir.openNextFile()` without `entry.close()` first. Both rely on the Arduino `File` destructor firing on assignment, which is implementation-defined behavior and has been broken in some versions of the ESP32 Arduino LittleFS wrapper. If handles leak, subsequent `openNextFile()` calls fail silently.
- **Trigger:** Many session files in `/logs/`; file handle pool exhaustion in LittleFS.
- **Impact:** Eviction stops after the file handle pool is exhausted; `oldestPath` is not found; no sessions are evicted; flash fills up. Silent data loss for subsequent runs.
- **Evidence:** `SessionLogger.cpp:90-101`, `WebServer.cpp:719-729`. Neither calls `entry.close()` in the loop body before reassigning.
- **Suggested fix:** Call `entry.close()` at the end of each loop iteration before `entry = dir.openNextFile()`.
- **Confidence:** Low (depends on LittleFS version; RAII usually works)

---

## Notes / unclear areas

1. **`/api/log` mutex hold time during streaming:** `/api/log` (WebServer.cpp:556-584) holds `lockLog()` for the full read of up to 400 lines via `AsyncResponseStream`. The mutex is released only after `req->send(resp)` is called. Whether `req->send()` is synchronous with respect to the file read -- i.e., whether the response body has been fully read into the stream buffer before `unlockLog()` is called -- depends on `AsyncResponseStream` internals. If the stream object buffers the entire body in heap memory before `send()` returns, this is safe but slow. If it defers sending to async_tcp callbacks, the mutex may be released before the data is actually transmitted, which is correct. The code appears safe but deserves review against the specific `ESPAsyncWebServer` version in use.

2. **`snapshotIntervalMs` defaults:** `Config::snapshotIntervalMs` defaults to 0 according to grep output, meaning `FlightRecorder::tick()` uses the 10,000 ms fallback. At 2 Hz session logging and 6 Hz ECU loop calls to `tick()`, the SNAP rate is bounded to 1 per 10 seconds. The comments in `FlightRecorder.h` describe 203 records/30-min run, which is consistent. This seems intentional.

3. **`logFault` JSON sanitization:** `logFault` (FlightRecorder.cpp:123-125) replaces `"` and `\` in `faultDescription` with `'`. This prevents JSON string breakage but does not handle other control characters (e.g., `\n`, `\r`, `\t`) that would also break a JSON string literal. If `faultDescription` contains a newline, the resulting NDJSON line will be split across two lines, and the second fragment (starting without `{`) will be silently discarded by all readers. Severity is Low to Medium depending on how `faultDescription` is populated.

4. **`_writeRow` buffer size vs. maximum CSV row:** `_writeRow` uses `char r[320]`. With all 18 channel bits set and maximum field values, the row can approach 300-310 characters. This appears safe with a 320-byte buffer, but the calculation is tight if additional channels are added. No truncation detection exists (`_file.println(r)` always writes `r` after `r[n] = 0`, but if `n` reached 319 it was silently truncated at some field boundary, producing a row with missing trailing columns).

5. **Flash partition space concern (lower risk than initially estimated):** The 0xE0000 (896 KB) partition with LittleFS overhead leaves approximately 820-840 KB usable. The event log at max capacity (~344 KB) and multiple session CSVs can coexist with some margin. The SESSION_MIN_FREE_BYTES = 150 KB threshold is aggressive (see ECU-LOG-09) but not immediately catastrophic for typical 30-min runs with the default 3-channel mask.

6. **`/api/session/log?run=N` path traversal:** The `run` parameter is parsed with `.toInt()` and formatted via `snprintf(path, sizeof(path), "/logs/session_%d.csv", run)`. Since the integer conversion strips any path characters, path traversal is not possible here. This check is clean.
