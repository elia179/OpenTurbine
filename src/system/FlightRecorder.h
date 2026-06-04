#pragma once
#include "../engine/EngineData.h"
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ============================================================
//  FlightRecorder — black-box event log to LittleFS
//
//  Combines lifecycle events (start/fault/shutdown/abort/config-change)
//  with compact 10-second SNAP records (N1, TOT, throttle + fitted
//  sensors) and a per-run RUN_SUMMARY (max N1, max TOT, min oil,
//  run duration).
//
//  Ring buffer of MAX_RECORDS.  At ~203 records per 30-min run, 2200
//  records covers ~10 complete runs.  Eviction drops the oldest 20 %
//  (~2 runs) so at least 8 full runs are always retained.
//
//  Written by ECU loop only. Web server reads for display/download.
//  Each write is synchronous (LittleFS open/append/close per event).
// ============================================================

class FlightRecorder {
public:
    static constexpr const char* PATH       = "/logs/events.json";
    static constexpr int         MAX_RECORDS = 2200;

    static void begin();
    static void tick();   // called every ECU loop — writes SNAP + tracks run peaks

    // Log specific events
    static void logBoot();
    static void logStartAttempt();
    static void logBlockEnter(const char* blockName);
    static void logBlockExit(const char* blockName, const char* result);
    static void logRunningEntry();
    static void logFault(const char* code);
    static void logNormalShutdown();
    static void logFaultShutdown(const char* code);
    static void logAbort(const char* blockName, const char* reason);
    static void logConfigChange(const char* field, float oldVal, float newVal);
    static void logRelight(uint8_t attemptNum);
    static void logRunSummary();   // called automatically by shutdown handlers

    static void clear();
    static void requestClear();  // Core 1: defer file removal to Core 0

    // Current number of records in the log (0 before first write).
    static int recordCount();

    // For web download — writes full log JSON to buf, returns bytes written
    static size_t toJson(char* buf, size_t len);

    // Bracket direct file access (e.g. CSV handler) with these to prevent
    // racing against _append()'s ring-buffer eviction (remove + rename).
    static void lockLog();
    static void unlockLog();
    static void beginRawDownload();
    static void endRawDownload();

    // Called from Core 0 (web task tick) to offload ring-buffer eviction off Core 1.
    // No-op unless a previous _append() set the eviction-pending flag.
    static void runEviction();

private:
    static void   _append(const char* eventJson);
    static uint32_t _uptimeSec();

    // Mutex guards file access between Core 1 (_append) and Core 0 (toJson/runEviction).
    // Protects the remove+rename sequence in ring-buffer eviction.
    static SemaphoreHandle_t _mutex;

    // Set by Core 1 when the ring buffer is full; cleared by Core 0 after eviction.
    static volatile bool _evictionPending;

    // SNAP timing
    static unsigned long _lastSnapshotMs;

    // Run-peak accumulators — reset at RUNNING_ENTRY, written in RUN_SUMMARY
    static float    _runMaxN1;
    static float    _runMaxTot;
    static float    _runMinOil;
    static uint32_t _runStartSec;
};
