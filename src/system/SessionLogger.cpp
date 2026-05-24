#include "SessionLogger.h"
#include "Config.h"
#include "../engine/EngineData.h"
#include "../engine/Types.h"
#include <LittleFS.h>
#include <Arduino.h>
#include <climits>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// ── Row snapshot passed through the inter-core queue ─────────
// Plain-old-data struct — Core 1 fills it, Core 0 writes it.
struct SessionRow {
    uint32_t t_ms;
    uint32_t mask;
    float    n1, n2, tot, tit, oilPressure, p1, p2;
    float    throttleDemand, battVoltage, fuelPressure, fuelFlow;
    float    glowPlugDemand, fuelPump2Demand, propPitchDemand, oilPumpPct;
    int      abMode;
    bool     abFlameOn;
    uint8_t  sysMode;   // SysMode cast to byte
};

static File              _file;
static volatile bool     _open      = false;
static uint32_t          _lastMs    = 0;
static uint32_t          _rowCount  = 0;
static char              _currentPath[40] = {};
static QueueHandle_t     _rowQueue  = nullptr;

const char* SessionLogger::currentPath() { return _currentPath; }

static const char* _modeStr(uint8_t m) {
    switch ((SysMode)m) {
        case SysMode::STANDBY:  return "STANDBY";
        case SysMode::STARTUP:  return "STARTUP";
        case SysMode::RUNNING:  return "RUNNING";
        case SysMode::SHUTDOWN: return "SHUTDOWN";
        case SysMode::FAULT:    return "FAULT";
        default:                return "?";
    }
}

// ── Write one queued row to the open file (Core 0 only) ──────
static void _writeRow(const SessionRow& row) {
    uint32_t mask = row.mask;
    char r[320];
    int n = snprintf(r, sizeof(r), "%lu", (unsigned long)row.t_ms);

    if (mask & Config::SLOG_MODE)       n += snprintf(r+n, sizeof(r)-n, ",%s",  _modeStr(row.sysMode));
    if (mask & Config::SLOG_N1)         n += snprintf(r+n, sizeof(r)-n, ",%.0f",(double)row.n1);
    if (mask & Config::SLOG_N2)         n += snprintf(r+n, sizeof(r)-n, ",%.0f",(double)row.n2);
    if (mask & Config::SLOG_TOT)        n += snprintf(r+n, sizeof(r)-n, ",%.1f",(double)row.tot);
    if (mask & Config::SLOG_TIT)        n += snprintf(r+n, sizeof(r)-n, ",%.1f",(double)row.tit);
    if (mask & Config::SLOG_OIL)        n += snprintf(r+n, sizeof(r)-n, ",%.2f",(double)row.oilPressure);
    if (mask & Config::SLOG_P1)         n += snprintf(r+n, sizeof(r)-n, ",%.2f",(double)row.p1);
    if (mask & Config::SLOG_P2)         n += snprintf(r+n, sizeof(r)-n, ",%.2f",(double)row.p2);
    if (mask & Config::SLOG_THR)        n += snprintf(r+n, sizeof(r)-n, ",%.1f",(double)(row.throttleDemand * 100.0f));
    if (mask & Config::SLOG_BATT)       n += snprintf(r+n, sizeof(r)-n, ",%.2f",(double)row.battVoltage);
    if (mask & Config::SLOG_FUEL_PRESS) n += snprintf(r+n, sizeof(r)-n, ",%.2f",(double)row.fuelPressure);
    if (mask & Config::SLOG_FUEL_FLOW)  n += snprintf(r+n, sizeof(r)-n, ",%.3f",(double)row.fuelFlow);
    if (mask & Config::SLOG_GLOW)       n += snprintf(r+n, sizeof(r)-n, ",%.0f",(double)(row.glowPlugDemand * 100.0f));
    if (mask & Config::SLOG_FP2)        n += snprintf(r+n, sizeof(r)-n, ",%.1f",(double)(row.fuelPump2Demand * 100.0f));
    if (mask & Config::SLOG_AB)         n += snprintf(r+n, sizeof(r)-n, ",%d,%d", row.abMode, row.abFlameOn ? 1 : 0);
    if (mask & Config::SLOG_PROP)       n += snprintf(r+n, sizeof(r)-n, ",%.1f",(double)(row.propPitchDemand * 100.0f));
    if (mask & Config::SLOG_OIL_PCT)    n += snprintf(r+n, sizeof(r)-n, ",%.1f",(double)row.oilPumpPct);

    r[n] = 0;
    _file.println(r);
    _rowCount++;
}

// ── One-time init ─────────────────────────────────────────────
void SessionLogger::begin() {
    if (!LittleFS.exists("/logs")) LittleFS.mkdir("/logs");
    if (!_rowQueue) _rowQueue = xQueueCreate(20, sizeof(SessionRow));
}

// ── Evict oldest session files if flash is low ────────────────
static constexpr size_t SESSION_MIN_FREE_BYTES = 150 * 1024;

static void _evictOldSessions() {
    while (LittleFS.totalBytes() - LittleFS.usedBytes() < SESSION_MIN_FREE_BYTES) {
        File dir = LittleFS.open("/logs");
        if (!dir) break;

        int  oldest     = INT_MAX;
        char oldestPath[40] = {};
        File entry = dir.openNextFile();
        while (entry) {
            int num = -1;
            // entry.name() may return the full path (/logs/session_N.csv) or just the
            // basename — strip the directory prefix so sscanf works either way.
            const char* ename = entry.name();
            const char* fname = strrchr(ename, '/');
            fname = fname ? fname + 1 : ename;
            if (sscanf(fname, "session_%d.csv", &num) == 1 && num < oldest) {
                oldest = num;
                snprintf(oldestPath, sizeof(oldestPath), "/logs/session_%d.csv", num);
            }
            entry = dir.openNextFile();
        }
        dir.close();

        if (oldest == INT_MAX) break;
        LittleFS.remove(oldestPath);
        Serial.printf("[SessionLogger] Evicted %s — flash low\n", oldestPath);
    }
}

// ── Open a new session file (Core 1, called at STARTUP) ──────
// One-time stall — acceptable before the engine enters tight loops.
void SessionLogger::startSession() {
    if (_open) {
        _open = false;
        vTaskDelay(pdMS_TO_TICKS(30));  // let Core 0 finish any in-progress drain
        _file.flush();
        _file.close();
    }

    _evictOldSessions();

    uint32_t run = EngineData::instance().runCount + 1;
    snprintf(_currentPath, sizeof(_currentPath), "/logs/session_%lu.csv", (unsigned long)run);

    _file = LittleFS.open(_currentPath, "w");
    if (!_file) {
        Serial.printf("[SessionLogger] Failed to create %s\n", _currentPath);
        return;
    }

    uint32_t mask = Config::sessionLogMask;
    _file.print("t_ms");
    if (mask & Config::SLOG_MODE)       _file.print(",mode");
    if (mask & Config::SLOG_N1)         _file.print(",n1_rpm");
    if (mask & Config::SLOG_N2)         _file.print(",n2_rpm");
    if (mask & Config::SLOG_TOT)        _file.print(",tot_c");
    if (mask & Config::SLOG_TIT)        _file.print(",tit_c");
    if (mask & Config::SLOG_OIL)        _file.print(",oil_bar");
    if (mask & Config::SLOG_P1)         _file.print(",p1_bar");
    if (mask & Config::SLOG_P2)         _file.print(",p2_bar");
    if (mask & Config::SLOG_THR)        _file.print(",thr_pct");
    if (mask & Config::SLOG_BATT)       _file.print(",batt_v");
    if (mask & Config::SLOG_FUEL_PRESS) _file.print(",fuel_press_bar");
    if (mask & Config::SLOG_FUEL_FLOW)  _file.print(",fuel_flow");
    if (mask & Config::SLOG_GLOW)       _file.print(",glow_pct");
    if (mask & Config::SLOG_FP2)        _file.print(",fp2_pct");
    if (mask & Config::SLOG_AB)         _file.print(",ab_mode,ab_flame");
    if (mask & Config::SLOG_PROP)       _file.print(",prop_pct");
    if (mask & Config::SLOG_OIL_PCT)    _file.print(",oil_pump_pct");
    _file.println();
    _file.flush();

    _rowCount = 0;
    _lastMs   = 0;
    _open     = true;
    Serial.printf("[SessionLogger] Session started — %u sensors → %s\n",
        __builtin_popcount(mask), _currentPath);
}

// ── Close session file (Core 1, called at STANDBY) ───────────
void SessionLogger::endSession() {
    if (!_open) return;

    // Signal Core 0 to stop draining, then wait one web-task cycle
    // to ensure any in-progress drainQueue() call has returned.
    _open = false;
    vTaskDelay(pdMS_TO_TICKS(30));

    // Drain any rows Core 0 hasn't written yet — file is now ours alone.
    if (_rowQueue) {
        SessionRow row;
        while (xQueueReceive(_rowQueue, &row, 0) == pdTRUE) _writeRow(row);
    }

    _file.flush();
    _file.close();
    Serial.printf("[SessionLogger] Session ended — %u rows\n", (unsigned)_rowCount);
}

// ── Core 1: snapshot sensor state → queue (no file I/O) ──────
void SessionLogger::tick() {
    if (!_open || !_rowQueue) return;

    uint32_t now      = millis();
    uint32_t interval = Config::sessionLogIntervalMs > 0 ? Config::sessionLogIntervalMs : 500;
    if (now - _lastMs < interval) return;
    _lastMs = now;

    auto&    ed   = EngineData::instance();
    uint32_t mask = Config::sessionLogMask;

    SessionRow row;
    row.t_ms            = now;
    row.mask            = mask;
    row.n1              = ed.n1Rpm;
    row.n2              = ed.n2Rpm;
    row.tot             = ed.tot;
    row.tit             = ed.tit;
    row.oilPressure     = ed.oilPressure;
    row.p1              = ed.p1;
    row.p2              = ed.p2;
    row.throttleDemand  = ed.throttleDemand;
    row.battVoltage     = ed.battVoltage;
    row.fuelPressure    = ed.fuelPressure;
    row.fuelFlow        = ed.fuelFlow;
    row.glowPlugDemand  = ed.glowPlugDemand;
    row.fuelPump2Demand = ed.fuelPump2Demand;
    row.propPitchDemand = ed.propPitchDemand;
    row.oilPumpPct      = ed.oilPumpPct;
    row.abMode          = (int)ed.abMode;
    row.abFlameOn       = ed.abFlameOn;
    row.sysMode         = (uint8_t)ed.mode;

    // Non-blocking — drops silently if Core 0 is behind by more than 20 rows
    xQueueSendToBack(_rowQueue, &row, 0);
}

// ── Core 0: write queued rows to flash (no file I/O on Core 1) ──
void SessionLogger::drainQueue() {
    if (!_open || !_rowQueue) return;
    uint32_t prevCount = _rowCount;
    SessionRow row;
    while (_open && xQueueReceive(_rowQueue, &row, 0) == pdTRUE) {
        _writeRow(row);
    }
    // Flush every 20 rows (~10 s at 500 ms log rate) — same durability as before
    // but the stall now lands on Core 0, not the ECU loop.
    if (_rowCount / 20 > prevCount / 20) _file.flush();
}
