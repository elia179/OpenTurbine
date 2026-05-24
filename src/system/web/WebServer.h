#pragma once
#include "../../system/CommandQueue.h"

// ============================================================
//  WebServer — ESPAsyncWebServer + WebSocket telemetry
//
//  Runs on Core 0 (AsyncWebServer is FreeRTOS-native).
//  Static files served from LittleFS (/index.html, etc.)
//  WebSocket (/ws) pushes EngineData snapshot at 500 ms.
//
//  REST endpoints:
//    GET  /              → index.html
//    GET  /api/data      → live EngineData JSON snapshot
//    GET  /api/config    → current config.json
//    POST /api/config    → upload new config.json
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

private:
    static void _setupRoutes();
};
