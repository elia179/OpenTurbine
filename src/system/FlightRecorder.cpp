#include "FlightRecorder.h"
#include "Config.h"
#include "hardware_profile.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <esp_efuse.h>

unsigned long     FlightRecorder::_lastSnapshotMs = 0;
SemaphoreHandle_t FlightRecorder::_mutex          = nullptr;

// Tracked in-memory line count so we don't re-count the file on every append.
// -1 means not yet initialised (counted on first _append call).
static int s_lineCount = -1;

void FlightRecorder::begin() {
    if (!LittleFS.exists("/logs")) {
        LittleFS.mkdir("/logs");
    }
    _lastSnapshotMs = 0;
    s_lineCount     = -1;   // force recount on first write
    if (!_mutex) _mutex = xSemaphoreCreateMutex();
}

void FlightRecorder::tick() {
    auto& ed = EngineData::instance();
    if (ed.mode != SysMode::RUNNING) return;
    unsigned long now = millis();
    if (now - _lastSnapshotMs < Config::snapshotIntervalMs) return;
    _lastSnapshotMs = now;

    char buf[160];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"RUNNING_SNAP\",\"n1\":%.0f,\"tot\":%.1f,\"oil\":%.2f,\"thr\":%.2f}",
        _now(), ed.n1Rpm, ed.tot, ed.oilPressure, ed.throttleDemand);
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
        _now(), (unsigned long)EngineData::instance().bootCount, OT_PROFILE_ID, chipHex);
    _append(buf);
}

void FlightRecorder::logStartAttempt() {
    auto& ed = EngineData::instance();
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"START_ATTEMPT\",\"n1\":%.0f,\"oil\":%.2f,\"tot\":%.1f}",
        _now(), ed.n1Rpm, ed.oilPressure, ed.tot);
    _append(buf);
}

void FlightRecorder::logBlockEnter(const char* blockName) {
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"t\":%lu,\"ev\":\"BLOCK_ENTER\",\"block\":\"%s\"}",
        _now(), blockName);
    _append(buf);
}

void FlightRecorder::logBlockExit(const char* blockName, const char* result) {
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"t\":%lu,\"ev\":\"BLOCK_EXIT\",\"block\":\"%s\",\"res\":\"%s\"}",
        _now(), blockName, result);
    _append(buf);
}

void FlightRecorder::logRunningEntry() {
    auto& ed = EngineData::instance();
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"RUNNING_ENTRY\",\"n1\":%.0f,\"oil\":%.2f,\"tot\":%.1f}",
        _now(), ed.n1Rpm, ed.oilPressure, ed.tot);
    _append(buf);
}

void FlightRecorder::logFault(const char* code) {
    auto& ed = EngineData::instance();
    char buf[160];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"FAULT\",\"code\":\"%s\",\"n1\":%.0f,\"oil\":%.2f,\"tot\":%.1f}",
        _now(), code, ed.n1Rpm, ed.oilPressure, ed.tot);
    _append(buf);
}

void FlightRecorder::logNormalShutdown() {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"t\":%lu,\"ev\":\"NORMAL_SHUTDOWN\"}", _now());
    _append(buf);
}

void FlightRecorder::logFaultShutdown(const char* code) {
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"t\":%lu,\"ev\":\"FAULT_SHUTDOWN\",\"code\":\"%s\"}", _now(), code);
    _append(buf);
}

void FlightRecorder::logAbort(const char* blockName, const char* reason) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"ABORT\",\"block\":\"%s\",\"reason\":\"%s\"}",
        _now(), blockName, reason);
    _append(buf);
}

void FlightRecorder::logConfigChange(const char* field, float oldVal, float newVal) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"CONFIG_CHANGE\",\"field\":\"%s\",\"old\":%.4f,\"new\":%.4f}",
        _now(), field, oldVal, newVal);
    _append(buf);
}

void FlightRecorder::logCalibration(const char* type, float before, float after) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"CALIBRATION\",\"type\":\"%s\",\"before\":%.4f,\"after\":%.4f}",
        _now(), type, before, after);
    _append(buf);
}

void FlightRecorder::logRelight(uint8_t attemptNum) {
    auto& ed = EngineData::instance();
    char buf[160];
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,\"ev\":\"RELIGHT_ATTEMPT\",\"attempt\":%u,\"n1\":%.0f,\"tot\":%.1f}",
        _now(), (unsigned)attemptNum, ed.n1Rpm, ed.tot);
    _append(buf);
}

void FlightRecorder::clear() {
    LittleFS.remove(PATH);
    s_lineCount = 0;
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

// ── Private ───────────────────────────────────────────────────

void FlightRecorder::_append(const char* eventJson) {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);

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

    // Ring buffer eviction: when full, drop the oldest half
    if (s_lineCount >= MAX_RECORDS) {
        int keepFrom = MAX_RECORDS / 2;   // keep the newest half
        File fr = LittleFS.open(PATH, "r");
        File fw = LittleFS.open("/logs/events.tmp", "w");
        if (fr && fw) {
            int seen = 0;
            while (fr.available()) {
                String line = fr.readStringUntil('\n');
                line.trim();
                if (line.length() == 0 || line[0] != '{') continue;
                seen++;
                if (seen > keepFrom) {
                    fw.println(line.c_str());
                }
            }
        }
        if (fr) fr.close();
        if (fw) fw.close();
        LittleFS.remove(PATH);
        LittleFS.rename("/logs/events.tmp", PATH);
        s_lineCount = MAX_RECORDS - keepFrom;
    }

    // Append the new event
    File fa = LittleFS.open(PATH, "a");
    if (!fa) { if (_mutex) xSemaphoreGive(_mutex); return; }
    fa.println(eventJson);
    fa.close();
    s_lineCount++;
    if (_mutex) xSemaphoreGive(_mutex);
}

uint32_t FlightRecorder::_now() {
    return (uint32_t)(millis() / 1000);
}
