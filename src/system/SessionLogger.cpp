#include "SessionLogger.h"
#include "Config.h"
#include "../engine/EngineData.h"
#include "../engine/Types.h"
#include <LittleFS.h>
#include <Arduino.h>
#include <climits>
#include <cstring>
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
    float    loopHz, loopExecAvgMs, loopExecMaxMs;
    int      abMode;
    bool     abFlameOn;
    uint8_t  sysMode;   // SysMode cast to byte
};

static File              _file;
static volatile bool     _open      = false;
static volatile bool     _acceptRows = false;
static uint32_t          _lastMs    = 0;
static uint32_t          _rowCount  = 0;
static volatile uint32_t _droppedRows = 0;
static char              _currentPath[40] = {};
static QueueHandle_t     _rowQueue  = nullptr;
static volatile bool     _startPending = false;
static volatile bool     _endPending   = false;
static constexpr size_t  SESSION_MIN_FREE_BYTES = 150 * 1024;
static constexpr uint32_t SESSION_FREE_CHECK_MS = 5000;
static uint32_t          _lastFreeCheckMs = 0;
static bool              _lowSpaceDropActive = false;

const char* SessionLogger::currentPath() { return _currentPath; }

uint32_t SessionLogger::droppedRows() { return _droppedRows; }

static void _evictOldSessions();

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

static uint8_t _csvColumnCount(uint32_t mask) {
    uint8_t count = 1; // t_ms
    if (mask & Config::SLOG_MODE)       count += 1;
    if (mask & Config::SLOG_N1)         count += 1;
    if (mask & Config::SLOG_N2)         count += 1;
    if (mask & Config::SLOG_TOT)        count += 1;
    if (mask & Config::SLOG_TIT)        count += 1;
    if (mask & Config::SLOG_OIL)        count += 1;
    if (mask & Config::SLOG_P1)         count += 1;
    if (mask & Config::SLOG_P2)         count += 1;
    if (mask & Config::SLOG_THR)        count += 1;
    if (mask & Config::SLOG_BATT)       count += 1;
    if (mask & Config::SLOG_FUEL_PRESS) count += 1;
    if (mask & Config::SLOG_FUEL_FLOW)  count += 1;
    if (mask & Config::SLOG_GLOW)       count += 1;
    if (mask & Config::SLOG_FP2)        count += 1;
    if (mask & Config::SLOG_AB)         count += 2;
    if (mask & Config::SLOG_PROP)       count += 1;
    if (mask & Config::SLOG_OIL_PCT)    count += 1;
    if (mask & Config::SLOG_LOOP)       count += 3;
    return count;
}

// ── Write one queued row to the open file (Core 0 only) ──────
static void _writeRow(const SessionRow& row) {
    uint32_t mask = row.mask;
    char r[320];
    int n = snprintf(r, sizeof(r), "%lu", (unsigned long)row.t_ms);

    #define APPEND_ROW_FIELD(...) do { \
        if (n >= 0 && n < (int)sizeof(r)) { \
            int wrote = snprintf(r + n, sizeof(r) - (size_t)n, __VA_ARGS__); \
            if (wrote < 0) { \
                n = (int)sizeof(r) - 1; \
            } else { \
                n += wrote; \
                if (n >= (int)sizeof(r)) n = (int)sizeof(r) - 1; \
            } \
        } \
    } while (0)

    if (mask & Config::SLOG_MODE)       APPEND_ROW_FIELD(",%s",  _modeStr(row.sysMode));
    if (mask & Config::SLOG_N1)         APPEND_ROW_FIELD(",%.0f",(double)row.n1);
    if (mask & Config::SLOG_N2)         APPEND_ROW_FIELD(",%.0f",(double)row.n2);
    if (mask & Config::SLOG_TOT)        APPEND_ROW_FIELD(",%.1f",(double)row.tot);
    if (mask & Config::SLOG_TIT)        APPEND_ROW_FIELD(",%.1f",(double)row.tit);
    if (mask & Config::SLOG_OIL)        APPEND_ROW_FIELD(",%.2f",(double)row.oilPressure);
    if (mask & Config::SLOG_P1)         APPEND_ROW_FIELD(",%.2f",(double)row.p1);
    if (mask & Config::SLOG_P2)         APPEND_ROW_FIELD(",%.2f",(double)row.p2);
    if (mask & Config::SLOG_THR)        APPEND_ROW_FIELD(",%.1f",(double)(row.throttleDemand * 100.0f));
    if (mask & Config::SLOG_BATT)       APPEND_ROW_FIELD(",%.2f",(double)row.battVoltage);
    if (mask & Config::SLOG_FUEL_PRESS) APPEND_ROW_FIELD(",%.2f",(double)row.fuelPressure);
    if (mask & Config::SLOG_FUEL_FLOW)  APPEND_ROW_FIELD(",%.3f",(double)row.fuelFlow);
    if (mask & Config::SLOG_GLOW)       APPEND_ROW_FIELD(",%.0f",(double)(row.glowPlugDemand * 100.0f));
    if (mask & Config::SLOG_FP2)        APPEND_ROW_FIELD(",%.1f",(double)(row.fuelPump2Demand * 100.0f));
    if (mask & Config::SLOG_AB)         APPEND_ROW_FIELD(",%d,%d", row.abMode, row.abFlameOn ? 1 : 0);
    if (mask & Config::SLOG_PROP)       APPEND_ROW_FIELD(",%.1f",(double)(row.propPitchDemand * 100.0f));
    if (mask & Config::SLOG_OIL_PCT)    APPEND_ROW_FIELD(",%.1f",(double)row.oilPumpPct);
    if (mask & Config::SLOG_LOOP)       APPEND_ROW_FIELD(",%.1f,%.3f,%.3f",
                                                         (double)row.loopHz,
                                                         (double)row.loopExecAvgMs,
                                                         (double)row.loopExecMaxMs);

    #undef APPEND_ROW_FIELD

    r[sizeof(r) - 1] = 0;
    const uint32_t nowMs = millis();
    if (_lowSpaceDropActive || nowMs - _lastFreeCheckMs >= SESSION_FREE_CHECK_MS) {
        _lastFreeCheckMs = nowMs;
        _lowSpaceDropActive = (LittleFS.totalBytes() - LittleFS.usedBytes()) < SESSION_MIN_FREE_BYTES;
    }
    if (_lowSpaceDropActive) {
        _evictOldSessions();
        _lastFreeCheckMs = nowMs;
        _lowSpaceDropActive = (LittleFS.totalBytes() - LittleFS.usedBytes()) < SESSION_MIN_FREE_BYTES;
        if (_lowSpaceDropActive) {
            _droppedRows = _droppedRows + 1;
            return;
        }
    }
    if (_file.println(r) == 0) {
        _droppedRows = _droppedRows + 1;
        _lowSpaceDropActive = true;
        return;
    }
    _rowCount++;
}

// ── One-time init ─────────────────────────────────────────────
bool SessionLogger::begin() {
    if (!LittleFS.exists("/logs")) LittleFS.mkdir("/logs");
    if (!_rowQueue) _rowQueue = xQueueCreate(20, sizeof(SessionRow));
    return _rowQueue != nullptr;
}

// ── Evict oldest session files if flash is low ────────────────
static void _evictOldSessions() {
    while (LittleFS.totalBytes() - LittleFS.usedBytes() < SESSION_MIN_FREE_BYTES) {
        File dir = LittleFS.open("/logs");
        if (!dir) break;

        int  oldest     = INT_MAX;
        char oldestPath[40] = {};
        bool sawCurrent = false;
        File entry = dir.openNextFile();
        while (entry) {
            int num = -1;
            // entry.name() may return the full path (/logs/session_N.csv) or just the
            // basename — strip the directory prefix so sscanf works either way.
            const char* ename = entry.name();
            const char* fname = strrchr(ename, '/');
            fname = fname ? fname + 1 : ename;
            if (sscanf(fname, "session_%d.csv", &num) == 1) {
                char candidate[40];
                snprintf(candidate, sizeof(candidate), "/logs/session_%d.csv", num);
                if (_open && strcmp(candidate, _currentPath) == 0) {
                    sawCurrent = true;
                } else if (num < oldest) {
                    oldest = num;
                    strncpy(oldestPath, candidate, sizeof(oldestPath) - 1);
                    oldestPath[sizeof(oldestPath) - 1] = '\0';
                }
            }
            entry.close();
            entry = dir.openNextFile();
        }
        dir.close();

        if (oldest == INT_MAX) {
            if (sawCurrent) Serial.println("[SessionLogger] Flash low - current session is the only log left");
            break;
        }
        LittleFS.remove(oldestPath);
        Serial.printf("[SessionLogger] Evicted %s — flash low\n", oldestPath);
    }
}

// ── Core 0 lifecycle work requested by ECU transitions ───────
static void _openSession() {
    if (_open) {
        _acceptRows = false;
        _open = false;
        _file.flush();
        _file.close();
    }

    _evictOldSessions();

    uint32_t run = EngineData::instance().runCount + 1;
    do {
        snprintf(_currentPath, sizeof(_currentPath), "/logs/session_%lu.csv", (unsigned long)run++);
    } while (LittleFS.exists(_currentPath));

    _file = LittleFS.open(_currentPath, "w");
    if (!_file) {
        Serial.printf("[SessionLogger] Failed to create %s\n", _currentPath);
        _currentPath[0] = '\0';
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
    if (mask & Config::SLOG_LOOP)       _file.print(",loop_hz,loop_exec_avg_ms,loop_exec_max_ms");
    if (_file.println() == 0) {
        Serial.printf("[SessionLogger] Failed to write CSV header to %s\n", _currentPath);
        _file.close();
        _currentPath[0] = '\0';
        _droppedRows = _droppedRows + 1;
        return;
    }
    _rowCount = 0;
    _droppedRows = 0;
    _lastMs   = 0;
    _lastFreeCheckMs = 0;
    _lowSpaceDropActive = false;
    _open     = true;
    _acceptRows = true;
    Serial.printf("[SessionLogger] Session started — %u columns → %s\n",
        (unsigned)_csvColumnCount(mask), _currentPath);
}

static void _closeSession() {
    if (!_open) return;
    _acceptRows = false;

    // Drain any rows Core 0 hasn't written yet before marking the session
    // closed. Low-space eviction uses _open/_currentPath to protect the active
    // file, including this final close path.
    if (_rowQueue) {
        SessionRow row;
        while (xQueueReceive(_rowQueue, &row, 0) == pdTRUE) _writeRow(row);
    }

    _open = false;
    _file.flush();
    _file.close();
    Serial.printf("[SessionLogger] Session ended — %u rows\n", (unsigned)_rowCount);
}

// ── Core 1: snapshot sensor state → queue (no file I/O) ──────
void SessionLogger::startSession() {
    _acceptRows = false;
    _startPending = true;
}

void SessionLogger::endSession() {
    _acceptRows = false;
    _endPending = true;
    _startPending = false;
}

void SessionLogger::tick() {
    if (!_open || !_acceptRows || !_rowQueue) return;

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
    row.loopHz          = ed.loopHz;
    row.loopExecAvgMs   = ed.loopExecAvgMs;
    row.loopExecMaxMs   = ed.loopExecMaxMs;
    row.abMode          = (int)ed.abMode;
    row.abFlameOn       = ed.abFlameOn;
    row.sysMode         = (uint8_t)ed.mode;

    // Non-blocking — drops silently if Core 0 is behind by more than 20 rows
    if (xQueueSendToBack(_rowQueue, &row, 0) != pdTRUE) {
        _droppedRows = _droppedRows + 1;
    }
}

// ── Core 0: write queued rows to flash (no file I/O on Core 1) ──
void SessionLogger::drainQueue() {
    if (_endPending) {
        _endPending = false;
        _closeSession();
    }
    if (_startPending) {
        _startPending = false;
        _openSession();
    }
    if (!_open || !_rowQueue) return;
    SessionRow row;
    static constexpr uint8_t MAX_ROWS_PER_DRAIN = 2;
    uint8_t drained = 0;
    while (_open && drained < MAX_ROWS_PER_DRAIN &&
           xQueueReceive(_rowQueue, &row, 0) == pdTRUE) {
        _writeRow(row);
        drained++;
    }
    // Finalise storage on session close. Mid-run LittleFS flushes can block
    // network servicing for seconds while the ECU is being monitored.
}
