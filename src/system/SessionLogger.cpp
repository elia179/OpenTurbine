#include "SessionLogger.h"
#include "Config.h"
#include "../engine/EngineData.h"
#include "../engine/Types.h"
#include <LittleFS.h>
#include <Arduino.h>

static File      _file;
static bool      _open      = false;
static uint32_t  _lastMs    = 0;
static uint32_t  _rowCount  = 0;
static char      _currentPath[40] = {};

const char* SessionLogger::currentPath() { return _currentPath; }

static const char* _modeStr(SysMode m) {
    switch (m) {
        case SysMode::STANDBY:  return "STANDBY";
        case SysMode::STARTUP:  return "STARTUP";
        case SysMode::RUNNING:  return "RUNNING";
        case SysMode::SHUTDOWN: return "SHUTDOWN";
        case SysMode::FAULT:    return "FAULT";
        default:                return "?";
    }
}

// ── One-time init: ensure /logs directory exists ──────────────
void SessionLogger::begin() {
    if (!LittleFS.exists("/logs")) LittleFS.mkdir("/logs");
}

// ── Open a new session file (called at STARTUP) ───────────────
void SessionLogger::startSession() {
    // Close any previously open session
    if (_open) {
        _file.flush();
        _file.close();
        _open = false;
    }

    // Name the file after the current run count so each run is kept separately.
    // runCount is incremented when entering RUNNING, but we open the file at
    // STARTUP — add 1 so the filename matches the run that produced the data.
    uint32_t run = EngineData::instance().runCount + 1;
    snprintf(_currentPath, sizeof(_currentPath), "/logs/session_%lu.csv", (unsigned long)run);

    _file = LittleFS.open(_currentPath, "w");
    if (!_file) {
        Serial.printf("[SessionLogger] Failed to create %s\n", _currentPath);
        return;
    }

    // Write CSV header based on mask
    uint32_t mask = Config::sessionLogMask;
    _file.print("t_ms");
    if (mask & Config::SLOG_MODE) _file.print(",mode");
    if (mask & Config::SLOG_N1)   _file.print(",n1_rpm");
    if (mask & Config::SLOG_N2)   _file.print(",n2_rpm");
    if (mask & Config::SLOG_TOT)  _file.print(",tot_c");
    if (mask & Config::SLOG_OIL)  _file.print(",oil_bar");
    if (mask & Config::SLOG_P1)   _file.print(",p1_bar");
    if (mask & Config::SLOG_P2)   _file.print(",p2_bar");
    if (mask & Config::SLOG_THR)  _file.print(",thr_pct");
    _file.println();
    _file.flush();

    _open     = true;
    _rowCount = 0;
    _lastMs   = 0;   // force first row immediately
    Serial.printf("[SessionLogger] Session started — %u sensors → %s\n",
        __builtin_popcount(mask), _currentPath);
}

// ── Close session file (called when returning to STANDBY) ─────
void SessionLogger::endSession() {
    if (!_open) return;
    _file.flush();
    _file.close();
    _open = false;
    Serial.printf("[SessionLogger] Session ended — %u rows\n", (unsigned)_rowCount);
}

// ── Configurable-rate row writer ──────────────────────────────
void SessionLogger::tick() {
    if (!_open) return;

    uint32_t now      = millis();
    uint32_t interval = Config::sessionLogIntervalMs > 0 ? Config::sessionLogIntervalMs : 500;
    if (now - _lastMs < interval) return;
    _lastMs = now;

    auto& ed      = EngineData::instance();
    uint32_t mask = Config::sessionLogMask;

    char row[160];
    int n = snprintf(row, sizeof(row), "%lu", (unsigned long)now);

    if (mask & Config::SLOG_MODE) n += snprintf(row+n, sizeof(row)-n, ",%s",  _modeStr(ed.mode));
    if (mask & Config::SLOG_N1)   n += snprintf(row+n, sizeof(row)-n, ",%.0f",(double)ed.n1Rpm);
    if (mask & Config::SLOG_N2)   n += snprintf(row+n, sizeof(row)-n, ",%.0f",(double)ed.n2Rpm);
    if (mask & Config::SLOG_TOT)  n += snprintf(row+n, sizeof(row)-n, ",%.1f",(double)ed.tot);
    if (mask & Config::SLOG_OIL)  n += snprintf(row+n, sizeof(row)-n, ",%.2f",(double)ed.oilPressure);
    if (mask & Config::SLOG_P1)   n += snprintf(row+n, sizeof(row)-n, ",%.2f",(double)ed.p1);
    if (mask & Config::SLOG_P2)   n += snprintf(row+n, sizeof(row)-n, ",%.2f",(double)ed.p2);
    if (mask & Config::SLOG_THR)  n += snprintf(row+n, sizeof(row)-n, ",%.1f",(double)(ed.throttleDemand * 100.0f));

    row[n] = 0;
    _file.println(row);

    _rowCount++;
    // Flush to flash every 10 rows to survive unexpected power cuts
    if (_rowCount % 10 == 0) _file.flush();
}
