#pragma once

// ============================================================
//  SessionLogger — per-run CSV sensor log
//
//  Logs selected sensors at 1 Hz only while the engine is
//  active (STARTUP → RUNNING → SHUTDOWN).  Nothing is logged
//  while in STANDBY.
//
//  Lifecycle:
//    begin()        — called once in setup(); creates /logs/ dir
//    startSession() — called when mode enters STARTUP; opens new CSV
//    endSession()   — called when mode returns to STANDBY; closes CSV
//    tick()         — called every loop(); writes rows when file open
//
//  Each run creates a separate file named /logs/session_N.csv where N
//  is the current runCount from EngineData.  Files are kept until flash
//  runs low — old ones can be deleted from the Log page.
//
//  Which sensors are logged is controlled by Config::sessionLogMask.
// ============================================================

class SessionLogger {
public:
    // currentPath() returns the active session file path (valid after startSession()).
    // Used by the web server download endpoint.
    static const char* currentPath();

    static void begin();         // init (mkdir /logs); call once in setup()
    static void startSession();  // open new CSV, write header; call at STARTUP
    static void endSession();    // flush + close; call when returning to STANDBY
    static void tick();          // 1 Hz row writer; no-op when no session open
};
