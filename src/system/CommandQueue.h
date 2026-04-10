#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// ============================================================
//  CommandQueue — thread-safe one-way pipe: Web (Core 0) → ECU (Core 1)
//
//  FreeRTOS queue, capacity QUEUE_DEPTH.
//  push() is non-blocking (web task — drops if full).
//  drain() called at top of every ECU loop tick.
//  ECU never blocks waiting for commands.
// ============================================================

enum class OTCommand : uint8_t {
    START,
    STOP,
    FUEL_PRIME,
    OIL_PRIME,
    IGN_TEST,
    START_TEST,
    FUEL_SOL_TEST,
    IDLE_TEST,
    SET_OIL_DEMAND,       // fParam = bar target
    SET_OIL_PCT,          // iParam = percent
    TOGGLE_LIMP_MODE,
    TOGGLE_DYNAMIC_IDLE,
    TOGGLE_SAFETY_CHECKS, // DEV_MODE only
    TOGGLE_DEV_MODE,      // runtime dev mode — unlocks config during engine operation
    TOGGLE_BENCH_MODE,    // bench/debug: all sequencer waits proceed on timer, safety skipped
    EXTRA_COOLDOWN,       // toggle: run starter+oil in standby until TOT drops or timeout
    STARTER_ASSIST,       // iParam: 1=enable, 0=disable — low-RPM starter torque assist
    CLEAR_LOG,
    AB_FIRE,              // manual afterburner ignition (from web UI)
    AB_STOP,              // manual afterburner shutdown (from web UI)
    APPLY_CONFIG,         // re-apply block params from config (safe in STANDBY only)
};

struct OTPacket {
    OTCommand cmd;
    float     fParam = 0.0f;
    int       iParam = 0;
};

using CommandHandler = void(*)(const OTPacket&);

class CommandQueue {
public:
    static constexpr int QUEUE_DEPTH = 16;

    static void begin() {
        _queue = xQueueCreate(QUEUE_DEPTH, sizeof(OTPacket));
    }

    // Called from Core 0 (web handler) — non-blocking
    static bool push(const OTPacket& pkt) {
        if (!_queue) return false;
        return xQueueSendToBack(_queue, &pkt, 0) == pdTRUE;
    }

    // Called from Core 1 (ECU loop) — drains all pending commands
    static void drain(CommandHandler handler) {
        if (!_queue || !handler) return;
        OTPacket pkt;
        while (xQueueReceive(_queue, &pkt, 0) == pdTRUE) {
            handler(pkt);
        }
    }

private:
    static QueueHandle_t _queue;
};
