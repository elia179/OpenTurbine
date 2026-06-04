#pragma once
#include "../../system/CommandQueue.h"

// ============================================================
//  WebServer — ESPAsyncWebServer + WebSocket telemetry
//
//  Runs on Core 0 (AsyncWebServer is FreeRTOS-native).
//  Static files served from LittleFS (/index.html, etc.)
//  WebSocket (/ws) serves client-pulled EngineData snapshots.
//
//  REST endpoints:
//    GET  /              → index.html
//    GET  /api/data      → live EngineData JSON snapshot
//    GET  /api/config    → current settings section from ecu_config.json
//    POST /api/config    → replace settings section in ecu_config.json
//    GET  /api/log       → full flight recorder log
//    POST /api/command   → queue OTCommand
//    POST /api/start     → queue START
//    POST /api/stop      → queue STOP (high priority)
//    GET  /api/status    → mode + health summary
//    WS   /ws            → live telemetry push at 500 ms
// ============================================================

class WebServer {
public:
    static void begin();
    static void tick();
    static bool otaInProgress();

private:
    static void _setupRoutes();
};
