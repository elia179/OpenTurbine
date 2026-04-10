#pragma once
#include "../engine/EngineData.h"
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ============================================================
//  FlightRecorder — persistent event log to LittleFS
//
//  Written by ECU loop only. Web server reads for display/download.
//  Ring buffer — oldest records overwritten when full.
//  Each write is synchronous (LittleFS open/append/close per event).
// ============================================================

class FlightRecorder {
public:
    static constexpr const char* PATH       = "/logs/events.json";
    static constexpr int         MAX_RECORDS = 500;

    static void begin();
    static void tick();   // called every ECU loop — periodic snapshot in RUNNING

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
    static void logCalibration(const char* type, float before, float after);
    static void logRelight(uint8_t attemptNum);

    static void clear();

    // For web download — writes full log JSON to buf, returns bytes written
    static size_t toJson(char* buf, size_t len);

private:
    static void   _append(const char* eventJson);
    static uint32_t _now();

    static unsigned long    _lastSnapshotMs;
    // Mutex guards file access between Core 1 (_append) and Core 0 (toJson).
    // Protects the remove+rename sequence in ring-buffer eviction.
    static SemaphoreHandle_t _mutex;
};
