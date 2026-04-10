#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>

// ============================================================
//  PlatformInit — one-time ESP32 board bring-up
//
//  Serial, LittleFS, NVS boot counter, ADC attenuation.
//  Everything MCU-specific that doesn't belong in HAL.
// ============================================================

class PlatformInit {
public:
    static void begin() {
        Serial.begin(115200);
        delay(100);
        Serial.println("\n[OT] OpenTurbine booting — " OT_PROFILE_ID);

        // LittleFS
        if (!LittleFS.begin(true)) {  // true = format on fail
            Serial.println("[OT] LittleFS mount failed — formatted");
        } else {
            Serial.println("[OT] LittleFS OK");
        }

        // ADC: 12-bit, 11 dB attenuation (0–3.3V full range)
        analogSetWidth(12);
        analogSetAttenuation(ADC_11db);

        // NVS boot counter via Preferences
        Preferences prefs;
        prefs.begin("ot", false);
        uint32_t bc = prefs.getUInt("bootCount", 0) + 1;
        prefs.putUInt("bootCount", bc);
        prefs.end();
        EngineData::instance().bootCount = bc;

        Serial.printf("[OT] Boot #%lu\n", (unsigned long)bc);

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
