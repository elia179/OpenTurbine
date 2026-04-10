#pragma once
#include "../../system/CommandQueue.h"

// ============================================================
//  WebServer — ESPAsyncWebServer + WebSocket telemetry
//
//  Runs on Core 0 (AsyncWebServer is FreeRTOS-native).
//  Static files served from LittleFS (/index.html, etc.)
//  WebSocket pushes EngineData snapshot at WS_INTERVAL_MS.
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
//    WS   /ws            → live telemetry push
// ============================================================

class WebServer {
public:
    static void begin();
    static void tick();   // call from a Core 0 task or setup loop — pushes WS frames

private:
    static void _pushTelemetry();
    static void _setupRoutes();
    static void _onWsEvent(void* arg, uint8_t* data, size_t len);
    static unsigned long _lastWsMs;
};
