#pragma once
#include <esp_task_wdt.h>

// Hardware watchdog via ESP32 task watchdog timer (TWDT).
// If the ECU loop stalls for > TIMEOUT_S seconds, the ESP32 resets.
// Watchdog is subscribed to the current task (Core 1 loop).
class Watchdog {
public:
    static constexpr uint32_t TIMEOUT_S = 5;

    static void begin() {
        esp_task_wdt_config_t cfg = {
            .timeout_ms    = TIMEOUT_S * 1000,
            .idle_core_mask= 0,         // don't watch idle tasks
            .trigger_panic = true       // hard reset on timeout
        };
        esp_task_wdt_reconfigure(&cfg);
        esp_task_wdt_add(nullptr);      // subscribe loop task (Core 1) only
    }

    static void feed() {
        esp_task_wdt_reset();
    }
};
