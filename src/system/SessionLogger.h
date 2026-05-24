#pragma once

// ============================================================
//  SessionLogger — per-run CSV sensor log
//
//  Core 1 (ECU loop) calls tick() which snapshots EngineData
//  into a FreeRTOS queue — no file I/O on Core 1.
//  Core 0 (web task) calls drainQueue() to write queued rows
//  to flash, keeping all LittleFS access off the ECU core.
//
//  Lifecycle:
//    begin()        — called once in setup(); creates /logs/ dir
//    startSession() — called when mode enters STARTUP; opens new CSV
//    endSession()   — called when mode returns to STANDBY; closes CSV
//    tick()         — Core 1: queue push only, no file I/O
//    drainQueue()   — Core 0: writes queued rows to flash
// ============================================================

class SessionLogger {
public:
    // currentPath() returns the active session file path (valid after startSession()).
    static const char* currentPath();

    static void begin();         // init (mkdir /logs, create queue); call once in setup()
    static void startSession();  // open new CSV, write header; call at STARTUP
    static void endSession();    // drain remaining rows, flush + close; call at STANDBY
    static void tick();          // Core 1: snapshot → queue push (no file I/O)
    static void drainQueue();    // Core 0: write queued rows to flash
};
