#include "FlightRecorder.h"
#include "hardware_profile.h"
#include "Config.h"
#include "HardwareConfig.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <esp_efuse.h>

SemaphoreHandle_t FlightRecorder::_mutex          = nullptr;
volatile bool     FlightRecorder::_evictionPending = false;
unsigned long     FlightRecorder::_lastSnapshotMs  = 0;
float             FlightRecorder::_runMaxN1        = 0.0f;
float             FlightRecorder::_runMaxTot       = 0.0f;
float             FlightRecorder::_runMinOil       = 9999.0f;
uint32_t          FlightRecorder::_runStartSec     = 0;

// Tracked in-memory line count so we don't re-count the file on every append.
// -1 means not yet initialised (counted on first _append call).
static int s_lineCount = -1;

void FlightRecorder::begin() {
    if (!LittleFS.exists("/logs")) {
        LittleFS.mkdir("/logs");
    }
    s_lineCount = -1;   // force recount on first write
    if (!_mutex) _mutex = xSemaphoreCreateMutex();
}

void FlightRecorder::tick() {
    auto& ed = EngineData::instance();
    auto& hw = HardwareConfig::instance();

    // Track run peaks (only meaningful during RUNNING, but harmless otherwise)
    if (ed.n1Rpm  > _runMaxN1)                              _runMaxN1 = ed.n1Rpm;
    if (ed.tot    > _runMaxTot)                             _runMaxTot = ed.tot;
    if (hw.hasOilPress && ed.oilPressure < _runMinOil)      _runMinOil = ed.oilPressure;

    // Compact 10-second SNAP: essential sensors only
    uint32_t intervalMs = Config::snapshotIntervalMs > 0 ? Config::snapshotIntervalMs : 10000;
    unsigned long now = millis();
    if (now - _lastSnapshotMs < intervalMs) return;
    _lastSnapshotMs = now;

    // thr as 0-100 integer to save bytes
    int thrPct = (int)(ed.throttleDemand * 100.0f + 0.5f);

    char buf[200];
    int n = snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"SNAP\",\"n1\":%.0f,\"tot\":%.0f,\"thr\":%d",
        _uptimeSec(), ed.n1Rpm, ed.tot, thrPct);

    if (hw.hasOilPress)
        n += snprintf(buf+n, sizeof(buf)-n, ",\"oil\":%.2f,\"oilP\":%d",
                      ed.oilPressure, (int)ed.oilPumpPct);
    if (hw.hasTit)
        n += snprintf(buf+n, sizeof(buf)-n, ",\"tit\":%.0f", ed.tit);

    snprintf(buf+n, sizeof(buf)-n, "}");
    _append(buf);
}

void FlightRecorder::logBoot() {
    // Include EFUSE chip ID so logs from different boards can be correlated.
    uint64_t chipId = ESP.getEfuseMac();
    char chipHex[17];
    snprintf(chipHex, sizeof(chipHex), "%04X%08X",
             (unsigned)((chipId >> 32) & 0xFFFF),
             (unsigned)(chipId & 0xFFFFFFFF));
    char buf[160];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"bc\":%lu,\"ev\":\"BOOT\",\"profile\":\"%s\",\"chip\":\"%s\"}",
        _uptimeSec(), (unsigned long)EngineData::instance().bootCount, OT_PROFILE_ID, chipHex);
    _append(buf);
}

void FlightRecorder::logStartAttempt() {
    auto& ed = EngineData::instance();
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"START_ATTEMPT\",\"n1Rpm\":%.0f,\"oilBar\":%.2f,\"totDegC\":%.1f}",
        _uptimeSec(), ed.n1Rpm, ed.oilPressure, ed.tot);
    _append(buf);
}

void FlightRecorder::logBlockEnter(const char* blockName) {
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"t\":%lu,\"ev\":\"BLOCK_ENTER\",\"block\":\"%s\"}",
        _uptimeSec(), blockName);
    _append(buf);
}

void FlightRecorder::logBlockExit(const char* blockName, const char* result) {
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"t\":%lu,\"ev\":\"BLOCK_EXIT\",\"block\":\"%s\",\"res\":\"%s\"}",
        _uptimeSec(), blockName, result);
    _append(buf);
}

void FlightRecorder::logRunningEntry() {
    // Reset run-peak accumulators for the new run
    _runMaxN1    = 0.0f;
    _runMaxTot   = 0.0f;
    _runMinOil   = 9999.0f;
    _runStartSec = _uptimeSec();

    auto& ed = EngineData::instance();
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"RUNNING_ENTRY\",\"n1Rpm\":%.0f,\"oilBar\":%.2f,\"totDegC\":%.1f}",
        _uptimeSec(), ed.n1Rpm, ed.oilPressure, ed.tot);
    _append(buf);
}

void FlightRecorder::logFault(const char* code) {
    auto& ed = EngineData::instance();
    // Truncate faultDescription to 120 chars to keep the record within the
    // 500-byte snprintf buffer without risking overflow.
    char desc[121];
    strncpy(desc, ed.faultDescription, 120);
    desc[120] = '\0';
    // Sanitise for raw JSON embedding — replace chars that would break the string literal.
    for (char* p = desc; *p; p++) {
        if (*p == '"' || *p == '\\') *p = '\'';
    }
    char buf[500];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"FAULT\",\"code\":\"%s\","
        "\"n1Rpm\":%.0f,\"totDegC\":%.1f,\"titDegC\":%.1f,"
        "\"oilBar\":%.2f,\"oilTempC\":%.1f,\"fuelBar\":%.2f,\"battV\":%.2f,"
        "\"desc\":\"%s\"}",
        _uptimeSec(), code,
        ed.n1Rpm, ed.tot, ed.tit,
        ed.oilPressure, ed.oilTemp, ed.fuelPressure, ed.battVoltage,
        desc);
    _append(buf);
}

void FlightRecorder::logRunSummary() {
    // Skip if logRunningEntry() was never called this boot (engine faulted before RUNNING)
    if (_runStartSec == 0) return;

    uint32_t runS = _uptimeSec() - _runStartSec;
    char buf[160];
    int n = snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"RUN_SUMMARY\",\"runS\":%lu,\"maxN1\":%.0f,\"maxTot\":%.0f",
        _uptimeSec(), (unsigned long)runS, _runMaxN1, _runMaxTot);
    if (_runMinOil < 9000.0f)
        n += snprintf(buf+n, sizeof(buf)-n, ",\"minOil\":%.2f", _runMinOil);
    snprintf(buf+n, sizeof(buf)-n, "}");
    _append(buf);
    _runStartSec = 0;   // prevent duplicate summary if shutdown handlers chain
}

void FlightRecorder::logNormalShutdown() {
    logRunSummary();
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"t\":%lu,\"ev\":\"NORMAL_SHUTDOWN\"}", _uptimeSec());
    _append(buf);
}

void FlightRecorder::logFaultShutdown(const char* code) {
    logRunSummary();
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"t\":%lu,\"ev\":\"FAULT_SHUTDOWN\",\"code\":\"%s\"}", _uptimeSec(), code);
    _append(buf);
}

void FlightRecorder::logAbort(const char* blockName, const char* reason) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"ABORT\",\"block\":\"%s\",\"reason\":\"%s\"}",
        _uptimeSec(), blockName, reason);
    _append(buf);
}

void FlightRecorder::logConfigChange(const char* field, float oldVal, float newVal) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"CONFIG_CHANGE\",\"field\":\"%s\",\"old\":%.4f,\"new\":%.4f}",
        _uptimeSec(), field, oldVal, newVal);
    _append(buf);
}

void FlightRecorder::logRelight(uint8_t attemptNum) {
    auto& ed = EngineData::instance();
    char buf[160];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"RELIGHT_ATTEMPT\",\"attempt\":%u,"
        "\"n1Rpm\":%.0f,\"totDegC\":%.1f,\"oilBar\":%.2f}",
        _uptimeSec(), (unsigned)attemptNum, ed.n1Rpm, ed.tot, ed.oilPressure);
    _append(buf);
}

int FlightRecorder::recordCount() {
    return s_lineCount < 0 ? 0 : s_lineCount;
}

void FlightRecorder::clear() {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    LittleFS.remove(PATH);
    s_lineCount = 0;
    if (_mutex) xSemaphoreGive(_mutex);
}

size_t FlightRecorder::toJson(char* buf, size_t len) {
    if (len < 4) return 0;

    // Take mutex so we don't read while _append() is mid-eviction (remove+rename).
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);

    File f = LittleFS.open(PATH, "r");
    if (!f || f.size() == 0) {
        if (f) f.close();
        if (_mutex) xSemaphoreGive(_mutex);
        buf[0]='['; buf[1]=']'; buf[2]=0;
        return 2;
    }

    size_t pos = 0;
    buf[pos++] = '[';
    bool first = true;

    while (f.available() && pos < len - 8) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line[0] != '{') continue;
        if (!first) { buf[pos++] = ','; }
        first = false;
        size_t ll = line.length();
        if (pos + ll >= len - 4) break;   // ran out of space
        memcpy(buf + pos, line.c_str(), ll);
        pos += ll;
    }

    buf[pos++] = ']';
    buf[pos]   = 0;
    f.close();
    if (_mutex) xSemaphoreGive(_mutex);
    return pos;
}

void FlightRecorder::lockLog() {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
}

void FlightRecorder::unlockLog() {
    if (_mutex) xSemaphoreGive(_mutex);
}

// ── Core 0 eviction (offloaded from ECU loop) ─────────────────

void FlightRecorder::runEviction() {
    if (!_evictionPending) return;

    // portMAX_DELAY is safe — this runs on Core 0 (web task), not the ECU loop.
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return;

    // Re-check under mutex: Core 1 may have already cleared the flag.
    if (!_evictionPending) { xSemaphoreGive(_mutex); return; }

    int keepFrom = MAX_RECORDS / 5;   // drop oldest 20%, keep newest 80%
    File fr = LittleFS.open(PATH, "r");
    File fw = LittleFS.open("/logs/events.tmp", "w");
    if (!fr || !fw) {
        if (fr) fr.close();
        if (fw) fw.close();
    } else {
        int seen = 0;
        while (fr.available()) {
            String line = fr.readStringUntil('\n');
            line.trim();
            if (line.length() == 0 || line[0] != '{') continue;
            seen++;
            if (seen > keepFrom) fw.println(line.c_str());
        }
        fr.close();
        fw.close();
        LittleFS.remove(PATH);
        LittleFS.rename("/logs/events.tmp", PATH);
        s_lineCount = MAX_RECORDS - keepFrom;
    }
    _evictionPending = false;
    if (_mutex) xSemaphoreGive(_mutex);
}

// ── Private ───────────────────────────────────────────────────

void FlightRecorder::_append(const char* eventJson) {
    // Short timeout: _append runs on Core 1 (ECU loop). toJson() on Core 0 can hold
    // the mutex for ~250 ms reading a full log. portMAX_DELAY would stall safety checks.
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(8)) != pdTRUE) return;

    // Lazy-init: count existing lines on first call after boot/clear
    if (s_lineCount < 0) {
        s_lineCount = 0;
        File fc = LittleFS.open(PATH, "r");
        if (fc) {
            while (fc.available()) {
                String line = fc.readStringUntil('\n');
                line.trim();
                if (line.length() > 0 && line[0] == '{') s_lineCount++;
            }
            fc.close();
        }
    }

    // Ring buffer full — request eviction from Core 0 and drop this event.
    // The 100–200 ms file-copy is too long to run on Core 1 (ECU loop).
    if (s_lineCount >= MAX_RECORDS) {
        _evictionPending = true;
        if (_mutex) xSemaphoreGive(_mutex);
        return;
    }

    // Append the new event
    File fa = LittleFS.open(PATH, "a");
    if (!fa) { if (_mutex) xSemaphoreGive(_mutex); return; }
    fa.println(eventJson);
    fa.close();
    s_lineCount++;
    if (_mutex) xSemaphoreGive(_mutex);
}

uint32_t FlightRecorder::_uptimeSec() {
    return (uint32_t)(millis() / 1000);
}
