# Wireless-web subsystem audit

## Files reviewed

- `/home/user/openturbine/src/system/web/WebServer.h`
- `/home/user/openturbine/src/system/web/WebServer.cpp` (1172 lines, fully reviewed)
- `/home/user/openturbine/src/system/CommandQueue.h`
- `/home/user/openturbine/src/system/Config.h`
- `/home/user/openturbine/src/system/HardwareConfig.h`
- `/home/user/openturbine/src/engine/EngineData.h`
- `/home/user/openturbine/src/platform/esp32/PlatformInit.h`
- `/home/user/openturbine/src/main.cpp` (task pinning, command handler, ECU loop)

---

## Findings (Critical -> Info)

---

### ECU-WEB-01: Unauthenticated Remote Engine Start
- **Severity:** Critical
- **Bug class:** Missing access control
- **Location:** `WebServer.cpp:642-645` (`POST /api/start`)
- **Description:** `POST /api/start` unconditionally pushes `OTCommand::START` into the command queue with no authentication, IP restriction, or mode pre-check. The endpoint is registered as a plain zero-body handler -- no body needed, no session, no token. Any WiFi client that associates with the AP (open network by default, see ECU-WEB-09) can fire it with a single HTTP POST.
- **Trigger:** `curl -X POST http://192.168.4.1/api/start` from any associated station.
- **Impact:** Remote start of a turbine engine by an unintended actor. The ECU command handler does gate on `mode == STANDBY && Config::profileMatch` (main.cpp:1117), so a running engine cannot be double-started, but a cold engine in STANDBY can be remotely started at any time by any AP client. The same applies to `POST /api/stop` (line 648): while STOP is safer, it can abort an intentional run.
- **Evidence:** `WebServer.cpp:642`: `_server.on("/api/start", HTTP_POST, [](AsyncWebServerRequest* req) { CommandQueue::push({ OTCommand::START }); ... });` -- no gate whatsoever.
- **Suggested fix:** Require a shared secret (e.g. a per-boot token sent in a custom header or embedded in the POST body) for all engine-control endpoints (`/api/start`, `/api/stop`, `/api/command`). At minimum, store a random 32-bit nonce at boot, return it via `/api/status`, and require `X-OT-Token: <nonce>` on write endpoints.
- **Confidence:** High

---

### ECU-WEB-02: TOGGLE_DEV_MODE Exposed Without Mode Gate, Unlocks Safety Subsystem Live
- **Severity:** Critical
- **Bug class:** Missing access control / unsafe state transition
- **Location:** `WebServer.cpp:683` (`/api/command`), `main.cpp:1177-1186`
- **Description:** `POST /api/command {"cmd":"TOGGLE_DEV_MODE"}` is accepted in any engine mode with no authentication and no STANDBY requirement. Enabling dev mode at runtime sets `EngineData::devMode = true` (main.cpp:1178), which immediately bypasses `Config::isLocked()` (Config.cpp:378-384), allowing calibration overwrites during RUNNING. A follow-on `{"cmd":"TOGGLE_SAFETY_CHECKS"}` then sets `skipSafetyChecks = true` (main.cpp:1174), disabling the overspeed, overtemp, and low-oil safeties. Both commands are accepted by any unauthenticated AP client.
- **Trigger:** Two sequential POSTs from any AP client: `{"cmd":"TOGGLE_DEV_MODE"}` then `{"cmd":"TOGGLE_SAFETY_CHECKS"}`.
- **Impact:** Full safety system bypass on a live engine. With safety checks off, overspeed, overtemp, and oil-loss conditions are not caught by the ECU. This is an unintended-attacker-level risk since the AP is open by default.
- **Evidence:** `WebServer.cpp:683` dispatches `TOGGLE_DEV_MODE` with no mode or auth check. `main.cpp:1177`: `ed.devMode = !ed.devMode;` -- no STANDBY gate.
- **Suggested fix:** Gate `TOGGLE_DEV_MODE` on `mode == SysMode::STANDBY` in the ECU command handler. Additionally, add compile-time `#ifndef OT_DEV_MODE` to exclude the command dispatch from production builds entirely.
- **Confidence:** High

---

### ECU-WEB-03: Shared g_webRxBuf Has No Concurrent-Request Protection
- **Severity:** High
- **Bug class:** Memory safety / concurrency
- **Location:** `WebServer.cpp:57-59, 472-479, 501-506, 658-661, 876-879, 925-928, 986-987`
- **Description:** A single global `g_webRxBuf[8192]` and associated `g_webRxLen`/`g_webRxOverflow` state are shared across all chunked POST/PATCH handlers. The comment at line 53 claims "only one HTTP request is processed at a time", but ESPAsyncWebServer's async_tcp task CAN interleave upload callbacks from multiple simultaneous TCP connections. When two clients send chunked bodies concurrently -- e.g. Client A sends `POST /api/config` (large body) and Client B sends `POST /api/command` (small body) -- Client B's `index==0` chunk resets `g_webRxLen = 0` (line 472), wiping Client A's partially accumulated body. Client A's final chunk then fires, but `g_webRxLen` now reflects only Client B's content, causing Client B's payload to be parsed as a config update. Conversely, `g_webTxBuf` is used for response serialization and is overwritten if a second request's response is built before the first `req->send()` completes its async copy.
- **Trigger:** Two AP clients simultaneously POSTing to different endpoints. Unlikely in normal use but trivially forced by an attacker or a browser retrying a stalled request.
- **Impact:** Corrupted config write (Client B's JSON applied as if it were Client A's full config); potential deserialization of truncated/mismatched JSON leading to silent config corruption or rejected update.
- **Evidence:** `g_webRxLen` reset without mutex at lines 472, 501, 658, 876, 925, 986. Single buffer shared at lines 473-476, 502-505, 659-661, 877-879, 926-928.
- **Suggested fix:** Allocate per-request body buffers (the request object lives for the request's lifetime -- store body state in a small heap allocation hung off `req->_tempObject`), or enforce a request-level semaphore that rejects concurrent POSTs with 503. Given the 8 KB target, per-request heap allocation is straightforward.
- **Confidence:** High

---

### ECU-WEB-04: OTA STANDBY Guard Checked Only on First Chunk -- Engine Start Mid-Upload Bypasses Gate
- **Severity:** High
- **Bug class:** TOCTOU / missing re-check
- **Location:** `WebServer.cpp:831-858` (`POST /update` upload handler)
- **Description:** The STANDBY guard (`EngineData::instance().mode != SysMode::STANDBY`) is inside `if (!index)` -- it runs only when the first upload chunk arrives (line 831-836). Subsequent chunks skip the mode check entirely. If the ECU transitions from STANDBY to STARTUP/RUNNING between the first and last chunk (e.g. the physical start button is pressed during a long OTA upload), `Update.write()` continues accumulating firmware bytes into the OTA partition and `Update.end(true)` commits it. The next reboot will run the new firmware regardless of what happened during the upload window.
- **Trigger:** OTA upload initiated while in STANDBY; engine started mid-upload (physical button or concurrent `/api/start`); upload completes normally.
- **Impact:** Firmware flash during engine operation. The OTA reboot itself is deferred until `tick()` fires `ESP.restart()` (line 1153-1154) -- which runs in the web task and triggers a hard reset including all actuator outputs, potentially mid-run.
- **Evidence:** `WebServer.cpp:831`: `if (!index) { ... if (mode != STANDBY) { _otaError = true; return; } ... }`. Mode check absent in the `if (!_otaError) { Update.write(...) }` block at line 845.
- **Suggested fix:** Add a mode re-check at the start of every chunk's write path: `if (EngineData::instance().mode != SysMode::STANDBY) { _otaError = true; Update.abort(); return; }`. Use `Update.abort()` to clean up the partial partition write.
- **Confidence:** High

---

### ECU-WEB-05: No OTA Firmware Size or Content Validation
- **Severity:** High
- **Bug class:** Missing input validation / flash abuse
- **Location:** `WebServer.cpp:840` (`Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)`)
- **Description:** The OTA handler calls `Update.begin(UPDATE_SIZE_UNKNOWN, ...)`, imposing no upper bound on firmware size. Any payload is accepted up to the OTA partition capacity. While the ESP-IDF `Update` library internally validates the first-byte magic (`0xE9`) before committing, it does so only at `Update.end()`. Until then, every byte is written to flash, including arbitrary attacker-supplied data. Additionally, there is no rate-limit or in-progress check -- repeated `POST /update` requests each call `Update.begin()` anew, which aborts and restarts the write, causing repeated flash erase cycles with no throttle. Flash NOR cells have a limited erase endurance (~100K cycles); deliberate hammering can brick the partition.
- **Trigger:** Repeated `POST /update` with any body content from any AP client.
- **Impact:** (a) Arbitrary data written to OTA flash partition. (b) Deliberate erase-cycle exhaustion of OTA partition. (c) If the ESP-IDF magic check has bugs or the attacker knows the format, a crafted image can be flashed with no other gatekeeping.
- **Evidence:** `WebServer.cpp:840`: `Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)`. No size argument. No in-progress guard (`Update.isRunning()` not checked before calling `begin()`).
- **Suggested fix:** Supply a maximum firmware size to `Update.begin()`: read the OTA partition size from the partition table and pass it. Add an `Update.isRunning()` guard at the start of the handler to reject concurrent upload attempts with 409. Optionally require a pre-shared HMAC of the image file before accepting upload.
- **Confidence:** High

---

### ECU-WEB-06: PATCH /api/config Accepted During Any Non-Locked Mode, Including FAULT
- **Severity:** High
- **Bug class:** Missing state gate / unsafe config mutation
- **Location:** `WebServer.cpp:495-551` (PATCH /api/config handler)
- **Description:** `PATCH /api/config` is gated only by `Config::isLocked()` (line 513), which returns `false` in STANDBY and also in FAULT mode (Config.cpp:377-383). The handler applies a JSON merge to the running config, calls `Config::fromJson()` (which immediately updates all static `Config::*` fields in memory), and then pushes `OTCommand::APPLY_CONFIG` into the command queue. Safety parameters including `totLimit`, `oilRunningMin`, `rpmLimit`, and all sequence timings are live-updated. A client can call this while the engine is in FAULT to alter the fault-recovery thresholds before the operator reboots, or -- with `devMode` enabled via ECU-WEB-02 -- during RUNNING.
- **Trigger:** `PATCH /api/config` with `{"totLimit": 99999}` from any AP client when `mode == FAULT`.
- **Impact:** Safety limit corruption at a time when the operator may be focused on the fault condition. Combined with ECU-WEB-02, achieves live safety limit overwrite during RUNNING.
- **Evidence:** `WebServer.cpp:513`: `if (Config::isLocked())` -- FAULT is not locked. No `mode != STANDBY` check for PATCH.
- **Suggested fix:** Apply the same STANDBY-only gate used by POST /api/hardware: `if (EngineData::instance().mode != SysMode::STANDBY) { req->send(409, ...); return; }`. If calibration changes during FAULT are a product requirement, limit the permitted fields to non-safety parameters.
- **Confidence:** High

---

### ECU-WEB-07: WebSocket Full-Frame (7 KB) Can Silently Drop on Connect
- **Severity:** Medium
- **Bug class:** Logic error / silent data loss
- **Location:** `WebServer.cpp:1093-1096` (WS event callback)
- **Description:** The WebSocket event handler uses a static `char buf[7168]`. The comments at lines 102 and 1058 both note that a full telemetry frame is "~7 KB". `serializeJson` returns `n`, and the frame is only sent when `n < sizeof(buf)` (line 1096). If the serialized full frame equals or exceeds 7168 bytes, the frame is silently dropped -- no error, no partial send, no fallback. The WS connect event (`WS_EVT_CONNECT`) always sends a full frame, so if the current config produces a frame >= 7168 bytes, the dashboard never receives its initial state and remains blank until the next ~30-second full-frame cycle. With a growing hardware config (many DI channels, long labels, long sequences), this threshold is realistic.
- **Trigger:** Connect with a hardware config that produces a full JSON frame of >= 7168 bytes. Growing `seqIssues`, long `labelXxx` strings, or many active DI channels push the frame toward the limit.
- **Impact:** Dashboard shows no data after connect. Operator has no telemetry. No error is logged or returned to the client.
- **Evidence:** `WebServer.cpp:1093`: `static char buf[7168]`. Line 1096: `if (n < sizeof(buf)) client->text(buf);` -- no else branch.
- **Suggested fix:** Increase `buf` to 9216 bytes (matching the full-frame use case) or dynamically heap-allocate based on `_buildTelemetry`'s actual output size. At minimum, log a warning and send the fast frame as a fallback when the full frame is truncated.
- **Confidence:** High

---

### ECU-WEB-08: Shared Static JsonDocument in WS Callback Is Not Re-Entrant for Multiple Clients
- **Severity:** Medium
- **Bug class:** Concurrency / shared mutable state
- **Location:** `WebServer.cpp:1094` (WS event callback `static JsonDocument doc`)
- **Description:** The WebSocket event lambda declares `static char buf[7168]` and `static JsonDocument doc` (lines 1093-1094). These statics are shared across all client instances. ESPAsyncWebServer's WS event handler fires in the async_tcp task, which can fire `WS_EVT_DATA` for Client A and then immediately fire `WS_EVT_CONNECT` (triggering a send) for Client B within the same event-loop pass. Both paths call `_buildTelemetry(buf, sizeof(buf), doc, ...)`, overwriting `buf` and `doc`. If Client A's `client->text(buf)` has not finished copying `buf` before Client B's `_buildTelemetry` overwrites it, Client A receives Client B's frame. Additionally, `_fullCounter` (line 1076, another static local in WS_EVT_DATA) is shared across all clients' DATA events, making the 30-second full-frame interval client-count-dependent.
- **Trigger:** Two or more WebSocket clients connected simultaneously; the second client connects while the first's frame is being built.
- **Impact:** Clients receive each other's telemetry frames, or stale frame data. Full-frame interval compressed by N clients.
- **Evidence:** `WebServer.cpp:1093-1094`: `static char buf[7168]; static JsonDocument doc;` inside the lambda shared by all WS clients. `AsyncWebSocketClient::text()` queues an async send -- the buffer may be reused before the queued data is transmitted.
- **Suggested fix:** Use `AsyncWebSocket::textAll()` with a single shared build pass, or allocate per-client send buffers. At minimum, check `_ws.count() <= 1` and log if multiple clients connect.
- **Confidence:** Medium

---

### ECU-WEB-09: Open WiFi AP by Default, Password Optional with No Warning Enforced
- **Severity:** Medium
- **Bug class:** Fail-open security configuration
- **Location:** `WebServer.cpp:71-72` (`_startWiFi`), `HardwareConfig.h:33`
- **Description:** The AP password is read from `HardwareConfig::wifiPassword[32]`. If the field is empty (the factory default), `WiFi.softAP(ssid, nullptr)` is called, creating an open (unencrypted, unauthenticated) AP. The log message notes "(open network)" but there is no runtime enforcement, warning LED, or refusal to start without a password. Because there is no authentication on any REST or WebSocket endpoint, an open AP means any device within radio range can associate and issue engine control commands, including START (ECU-WEB-01) and TOGGLE_DEV_MODE (ECU-WEB-02).
- **Trigger:** Default factory configuration, or configuration where `wifiPassword` was cleared.
- **Impact:** Zero-click remote engine control from any nearby WiFi device. Exacerbated by captive portal DNS redirecting all DNS queries to the ECU dashboard, ensuring any associated client is immediately served the control UI.
- **Evidence:** `WebServer.cpp:71`: `const char* pwd = HardwareConfig::wifiPassword[0] ? HardwareConfig::wifiPassword : nullptr;`. `HardwareConfig.h:33`: `static char wifiPassword[32];` -- no initialization shown, defaults to empty.
- **Suggested fix:** Require a non-empty `wifiPassword` before starting the AP; generate a random default password at first boot (store in NVS) and print it to Serial. Add a persistent fault state if the AP is running open, visible in the web UI and status LED.
- **Confidence:** High

---

### ECU-WEB-10: g_webRxBuf Overflow Check Is Off-by-One Allowing 8191-Byte Write Without Null Terminator
- **Severity:** Medium
- **Bug class:** Integer / buffer boundary
- **Location:** `WebServer.cpp:473, 502, 659, 877, 926, 987`
- **Description:** The overflow guard condition is `g_webRxLen + len < sizeof(g_webRxBuf)` (strictly less than). This permits writing up to 8191 bytes into an 8192-byte buffer (`g_webRxLen` can reach 8191). `g_webRxBuf[8191]` is the last valid byte; no null terminator is ever written after the accumulated body. ArduinoJson's `deserializeJson(doc, buf, len)` uses the length parameter, so it does not over-read, and this is not an exploitable buffer overflow. However, `Config::fromJson(g_webRxBuf, g_webRxLen)` and `HardwareConfig::fromJson(g_webRxBuf, g_webRxLen)` pass the raw buffer and length; if those functions internally treat the buffer as a C-string at any point (e.g. `Serial.printf`), the missing terminator could cause a read past the buffer into adjacent BSS. The adjacent variable is `g_webRxLen` (a `size_t`), so over-read would print a few garbage bytes -- low immediate impact, but the assumption that the buffer is always null-terminated is wrong.
- **Trigger:** POST body of exactly 8191 bytes.
- **Impact:** Potential one-to-few byte read past buffer end in diagnostic print paths; no confirmed code execution impact.
- **Evidence:** `WebServer.cpp:473`: `if (g_webRxLen + len < sizeof(g_webRxBuf))` -- `sizeof` is 8192; condition allows `g_webRxLen` to reach 8191 with no null written.
- **Suggested fix:** Change the condition to `g_webRxLen + len < sizeof(g_webRxBuf) - 1` (reserve one byte for null), and write `g_webRxBuf[g_webRxLen] = '\0'` before parsing. Apply uniformly across all six copy sites.
- **Confidence:** Medium

---

### ECU-WEB-11: Command Queue Silent Drop Loses Safety-Critical STOP/AB_STOP on Overflow
- **Severity:** Medium
- **Bug class:** Reliability / silent failure
- **Location:** `CommandQueue.h:68-71` (`push()`), `WebServer.cpp:648-651`
- **Description:** `CommandQueue::push()` calls `xQueueSendToBack(..., 0)` -- non-blocking, zero timeout. If the queue is full (depth 16), the command is silently dropped and `push()` returns `false`. The web handlers at lines 643 and 649 ignore the return value: `CommandQueue::push({ OTCommand::STOP });` then `req->send(200, ...)`. The operator receives `{"ok":true}` even when the STOP command was discarded. Under a flood of concurrent commands (e.g. a stuck browser retrying), the queue can fill with lower-priority commands (RESET_PEAKS, CLEAR_LOG) and cause STOP or AB_STOP to be dropped. AB_STOP dropped during an afterburner fault could leave the AB pump running.
- **Trigger:** Sixteen commands already queued when STOP or AB_STOP arrives. Possible under rapid web UI interaction or concurrent browser tabs.
- **Impact:** Operator command lost; engine or afterburner continues running when the operator intends it to stop. A `{"ok":true}` response creates false assurance.
- **Evidence:** `WebServer.cpp:649`: `CommandQueue::push({ OTCommand::STOP });` -- return value discarded. `WebServer.cpp:650`: `req->send(200, "application/json", "{\"ok\":true}")` always returns success.
- **Suggested fix:** Return the result of `push()` in the response: `bool ok = CommandQueue::push(...); req->send(ok ? 200 : 503, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"queue full\"}");`. For STOP and AB_STOP specifically, consider using `xQueueSendToFront` with a timeout to preempt lower-priority items.
- **Confidence:** High

---

### ECU-WEB-12: No HTTP Body Size Limit on AsyncWebServer; 8 KB Shared Buffer Is Only Guard
- **Severity:** Medium
- **Bug class:** DoS / resource exhaustion
- **Location:** `WebServer.cpp:62` (`_server` declaration), no `setBodyLimit` call
- **Description:** ESPAsyncWebServer accepts no body-size configuration at the server level (no call to `setBodyLimit` or equivalent). The only protection against oversized POST bodies is the application-level overflow guard on `g_webRxBuf`. However, the TCP segments and async_tcp receive buffers accumulate until the async task fires the upload callback with chunk data. A client that sends an arbitrarily large chunked body (e.g. a 512 KB POST to `/api/config`) causes async_tcp to buffer incoming data until callback fires repeatedly, potentially exhausting the TCP PCB pool or triggering `CONFIG_ASYNC_TCP_QUEUE_SIZE` limits. In practice this is a memory-pressure DoS, not an overflow, because the callback will set `g_webRxOverflow = true` after 8 KB and send 400 -- but the data is already in the TCP/IP stack.
- **Trigger:** `POST /api/config` with a very large body from any AP client.
- **Impact:** async_tcp queue fill, potential lwIP memory pool exhaustion, TCP connection stalls for other clients including the live WebSocket telemetry connection.
- **Evidence:** `WebServer.cpp:62`: `static AsyncWebServer _server(80);` -- no size limit configured. No `_server.setBodyLimit(...)` call found anywhere in the file.
- **Suggested fix:** Call `_server.setBodyLimit(8192 + 512)` (or the library equivalent) immediately after construction so async_tcp discards oversized bodies before they are queued rather than after 8 KB has been buffered.
- **Confidence:** Medium

---

### ECU-WEB-13: Static File Serving from LittleFS Root Allows Enumeration of All Filesystem Files
- **Severity:** Low
- **Bug class:** Information disclosure
- **Location:** `WebServer.cpp:436` (`serveStatic("/", LittleFS, "/")`)
- **Description:** `_server.serveStatic("/", LittleFS, "/")` serves files from the LittleFS root with no path restriction. An attacker who knows or guesses filenames can download `ecu_config.json` (which contains WiFi password, all calibration data, safety limits, hardware pin assignments, and sequence config) by requesting `GET /ecu_config.json` or `GET /hardware.json` directly from the static file server. The explicit `GET /api/config` and `GET /api/hardware` endpoints are the intended access paths, but the static server does not exclude config file paths.
- **Trigger:** `GET http://192.168.4.1/ecu_config.json` from any AP client.
- **Impact:** Full config disclosure including `wifiPassword`, all calibration polynomials, and safety limits. Information useful for crafting a targeted attack.
- **Evidence:** `WebServer.cpp:436`: `_server.serveStatic("/", LittleFS, "/")` with root as both the URL prefix and the FS root. `Config::PATH = "/ecu_config.json"` (Config.h:21).
- **Suggested fix:** Restrict `serveStatic` to a dedicated `/static/` subdirectory in LittleFS that contains only UI assets, not config files. Alternatively, add a `notFound` handler that intercepts direct requests for `*.json` files and returns 403.
- **Confidence:** High

---

### ECU-WEB-14: No Security Headers on Any HTTP Response
- **Severity:** Low
- **Bug class:** Defense-in-depth / hardening gap
- **Location:** `WebServer.cpp` (all `req->send(...)` calls, no header injection)
- **Description:** No HTTP response sets `Content-Security-Policy`, `X-Content-Type-Options`, `X-Frame-Options`, or `Cache-Control: no-store` on API responses. The captive portal HTML pages (lines 411-416) are served as bare HTML with no CSP, allowing inline script injection if any response body ever includes operator-supplied string content. While the ESP32 AP client base is small, a cross-site request forgery attack from a connected client's browser (which is already on the AP) could auto-POST to `/api/start` via a malicious captive-portal page from any other HTTP origin on the same AP.
- **Trigger:** Browser on the AP visits a crafted page (or the captive portal itself is tampered with via OTA ECU-WEB-05). Auto-POST to `/api/start`.
- **Impact:** CSRF-mediated engine start from a web page the operator did not intentionally open.
- **Evidence:** `WebServer.cpp:406-427`: captive portal handlers send bare HTML with no CSP. No `addHeader("X-Frame-Options", ...)` or `addHeader("Content-Security-Policy", ...)` anywhere.
- **Suggested fix:** Add `"Content-Security-Policy: default-src 'self'"` and `"X-Frame-Options: DENY"` to all responses via a global `_server.onNotFound` wrapper or a dedicated response-filter hook. For API endpoints, add `"Cache-Control: no-store"`.
- **Confidence:** Medium

---

## Notes / unclear areas

1. **Config::isLocked in FAULT mode**: The intent of allowing PATCH /api/config in FAULT mode is not documented. If it is intentional (allow operator to fix a bad calibration after a fault), the risk noted in ECU-WEB-06 should be acknowledged in a code comment and the permitted field set narrowed.

2. **ESPAsyncWebServer concurrency guarantee**: The comment at `WebServer.cpp:53` asserts that only one HTTP request is processed at a time. This is not a documented guarantee of ESPAsyncWebServer -- the library is async and can interleave callbacks for multiple simultaneous TCP connections. The finding ECU-WEB-03 should be validated against the specific AsyncTCP version in use (lib_deps in `platformio.ini`).

3. **OTA magic byte check**: The ESP-IDF `Update` library performs a firmware magic-byte check at `Update.end()`. The buffer written by `Update.write()` is the OTA partition staging area, not yet booted. A crafted image with a valid magic byte but corrupt content will fail the bootloader signature/CRC check on the next boot. This reduces but does not eliminate the risk from ECU-WEB-05 -- the OTA flash partition can still be deliberately trashed without producing a bootable image.

4. **EngineData cross-core reads without mutex**: The WebServer reads `EngineData` fields (Core 1 writes, Core 0 reads) without a mutex. The EngineData.h comment accepts this by design for 32-bit-aligned scalar reads (Xtensa atomic word access). Non-scalar composites such as `char lastEvent[64]`, `char faultDescription[192]`, and `SeqIssue seqIssues[]` are not atomically readable. A torn read of these char arrays in `_buildTelemetry` could produce a partially-updated string in the telemetry JSON. This is a display-only concern (acknowledged in EngineData.h line 28-29) and out of scope for safety, but could produce confusing fault descriptions in the UI.

5. **DNSServer query length validation**: The `DNSServer` library used for the captive portal is a third-party library not reviewed here. Some versions of the Arduino `DNSServer` have had buffer-size issues with oversized DNS query packets. This warrants a separate library audit.

6. **delay(200) in tick() on OTA restart**: `WebServer::tick()` calls `delay(200)` (line 1153) before `ESP.restart()`. This call blocks the web FreeRTOS task for 200 ms after `_otaPendingRestart` is set, during which no WebSocket frames are delivered. Minor UX issue, not a safety concern.
