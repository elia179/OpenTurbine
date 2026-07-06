#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_system.h>
#include "../../../hardware_profile.h"
#include "../../engine/EngineData.h"

// ============================================================
//  PlatformInit — one-time ESP32 board bring-up
//
//  Serial, LittleFS, NVS boot counter, ADC attenuation.
//  Everything MCU-specific that doesn't belong in HAL.
// ============================================================

class PlatformInit {
public:
    static void begin() {
        // Drive compiled relay outputs inactive before filesystem/config work.
        // External pull-offs are still required to guarantee the reset interval.
#ifdef OT_HAS_FUEL_SOL
        pinMode(OT_FUEL_SOL_PIN, OUTPUT);
        digitalWrite(OT_FUEL_SOL_PIN, OT_FUEL_SOL_ACTIVE_H ? LOW : HIGH);
#endif
#ifdef OT_HAS_IGNITER
        pinMode(OT_IGNITER_PIN, OUTPUT);
        digitalWrite(OT_IGNITER_PIN, OT_IGNITER_ACTIVE_H ? LOW : HIGH);
#endif
#ifdef OT_HAS_STARTER_EN
        pinMode(OT_STARTER_EN_PIN, OUTPUT);
        digitalWrite(OT_STARTER_EN_PIN, OT_STARTER_EN_ACTIVE_H ? LOW : HIGH);
#endif

        Serial.begin(115200);
        delay(100);
        Serial.println("\n[OT] OpenTurbine booting - default profile: " OT_PROFILE_ID);

        // LittleFS
        // Never format automatically on a control-system boot: a transient
        // mount failure must not erase configuration and logs.
        bool fsOk = LittleFS.begin(false, "/littlefs", 10, "littlefs");
        if (!fsOk) {
            Serial.println("[OT] ERROR: LittleFS mount failed - storage unavailable");
            auto& ed = EngineData::instance();
            ed.configLocked = true;
            ed.configStorageFault = true;
            strncpy(ed.faultDescription,
                    "Cannot start: LittleFS storage failed to mount. Config, calibration, web assets, and logs are unavailable.",
                    sizeof(ed.faultDescription) - 1);
            ed.faultDescription[sizeof(ed.faultDescription) - 1] = '\0';
        } else {
            Serial.println("[OT] LittleFS OK");
        }

        // ADC: 12-bit, 11 dB attenuation (0–3.3V full range)
        analogReadResolution(12);
        analogSetAttenuation(ADC_11db);

        // NVS boot counter via Preferences
        Preferences prefs;
        uint32_t bc = 1;
        if (prefs.begin("ot", false)) {
            bc = prefs.getUInt("bootCount", 0) + 1;
            if (prefs.putUInt("bootCount", bc) == 0) {
                Serial.println("[OT] WARNING: boot counter NVS write failed");
            }
            prefs.end();
        } else {
            Serial.println("[OT] WARNING: NVS unavailable - boot counter not persisted");
        }
        EngineData::instance().bootCount = bc;

        Serial.printf("[OT] Boot #%lu\n", (unsigned long)bc);

        // Log reset reason so we can diagnose unexpected reboots
        esp_reset_reason_t rr = esp_reset_reason();
        EngineData::instance().resetReason = (uint8_t)rr;
        const char* rrStr = "UNKNOWN";
        switch (rr) {
            case ESP_RST_POWERON:  rrStr = "POWER_ON";   break;
            case ESP_RST_SW:       rrStr = "SOFTWARE";   break;
            case ESP_RST_PANIC:    rrStr = "PANIC/CRASH"; break;
            case ESP_RST_INT_WDT:  rrStr = "INT_WDT";    break;
            case ESP_RST_TASK_WDT: rrStr = "TASK_WDT";   break;
            case ESP_RST_WDT:      rrStr = "WDT";        break;
            case ESP_RST_DEEPSLEEP:rrStr = "DEEP_SLEEP"; break;
            case ESP_RST_BROWNOUT: rrStr = "BROWNOUT";   break;
            default: break;
        }
        Serial.printf("[OT] Reset reason: %s (%d)\n", rrStr, (int)rr);

        // Stop pin pull-up
        pinMode(OT_STOP_PIN, INPUT_PULLUP);
#ifdef OT_START_PIN
        pinMode(OT_START_PIN, INPUT_PULLUP);
#endif

#ifdef OT_DEV_MODE
        EngineData::instance().devMode = true;
        Serial.println("[OT] *** DEV MODE ACTIVE ***");
#endif
    }
};
