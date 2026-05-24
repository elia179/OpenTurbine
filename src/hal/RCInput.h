#pragma once
#include <Arduino.h>
#include "../../hardware_profile.h"
#include "../system/Config.h"
#include "../engine/EngineData.h"

// ============================================================
//  RCInput — hardware-level RC PWM input (compile-time selection)
//
//  *** EXPERIMENTAL — not yet field-tested with full edge-case coverage.
//  *** Prefer ADC pot input for new builds until this is validated.
//  *** RC PWM is susceptible to glitches from RF interference; ensure
//  *** rcFailsafeMs is set conservatively and test failsafe behaviour
//  *** on the bench before flying.
//
//  Enabled by hardware_profile.h defines (see that file):
//    OT_IDLE_INPUT_RC_PWM      — servo PWM replaces ADC on OT_IDLE_INPUT_PIN
//    OT_THROTTLE_INPUT_RC_PWM  — servo PWM replaces ADC on OT_THROTTLE_INPUT_PIN
//
//  The GPIO pin is the SAME as the ADC version — only the signal
//  type changes.  Pulse width is calibrated via Config::rcMinUs /
//  rcMaxUs / rcFailsafeMs (tunable at runtime).
//
//  Outputs written:
//    ed.rcIdleValid / rcIdleNorm        (0.0–1.0) + synthesised idleInputRaw
//    ed.rcThrottleValid / rcThrottleNorm (0.0–1.0) + synthesised throttleInputRaw
//
//  Usage: RCInput::begin() once in setup(), RCInput::tick() each loop()
// ============================================================

class RCInput {
public:

    // ── Lifecycle ─────────────────────────────────────────────
    static void begin() {
#if defined(OT_IDLE_INPUT_RC_PWM) && defined(OT_HAS_IDLE_INPUT)
        _idle.pin = OT_IDLE_INPUT_PIN;
        pinMode(_idle.pin, INPUT);
        attachInterrupt(digitalPinToInterrupt(_idle.pin), _isrIdle, CHANGE);
        Serial.printf("[RCInput] idle input → servo PWM on GPIO%d  %d–%d µs\n",
            _idle.pin, Config::rcMinUs, Config::rcMaxUs);
#endif
#if defined(OT_THROTTLE_INPUT_RC_PWM) && defined(OT_HAS_THROTTLE_INPUT)
        _thr.pin = OT_THROTTLE_INPUT_PIN;
        pinMode(_thr.pin, INPUT);
        attachInterrupt(digitalPinToInterrupt(_thr.pin), _isrThr, CHANGE);
        Serial.printf("[RCInput] throttle input → servo PWM on GPIO%d  %d–%d µs\n",
            _thr.pin, Config::rcMinUs, Config::rcMaxUs);
#endif
    }

    // ── Called each loop tick ─────────────────────────────────
    static void tick() {
        auto& ed = EngineData::instance();

#if defined(OT_IDLE_INPUT_RC_PWM) && defined(OT_HAS_IDLE_INPUT)
        _updateCh(_idle, ed.rcIdleValid, ed.rcIdleNorm);
        if (ed.rcIdleValid) {
            // Synthesise idleInputRaw as equivalent ADC count (0–4095)
            ed.idleInputRaw = (int)(ed.rcIdleNorm * 4095.0f);
        }
#endif

#if defined(OT_THROTTLE_INPUT_RC_PWM) && defined(OT_HAS_THROTTLE_INPUT)
        _updateCh(_thr, ed.rcThrottleValid, ed.rcThrottleNorm);
        if (ed.rcThrottleValid) {
            // Synthesise throttleInputRaw as equivalent ADC count
            ed.throttleInputRaw = Config::throttleMinRaw +
                (int)(ed.rcThrottleNorm * (float)(Config::throttleMaxRaw - Config::throttleMinRaw));
        }
#endif
    }

private:

    struct Ch {
        int               pin     = -1;
        volatile uint32_t riseUs  = 0;
        volatile uint32_t pulseUs = 0;
        volatile bool     fresh   = false;
        uint32_t          lastMs  = 0;
    };

    static Ch _thr;
    static Ch _idle;

    static void IRAM_ATTR _isrThr() {
        if (digitalRead(_thr.pin) == HIGH) {
            _thr.riseUs = micros();
        } else {
            uint32_t pw = micros() - _thr.riseUs;
            if (pw >= 800 && pw <= 2200) { _thr.pulseUs = pw; _thr.fresh = true; }
        }
    }

    static void IRAM_ATTR _isrIdle() {
        if (digitalRead(_idle.pin) == HIGH) {
            _idle.riseUs = micros();
        } else {
            uint32_t pw = micros() - _idle.riseUs;
            if (pw >= 800 && pw <= 2200) { _idle.pulseUs = pw; _idle.fresh = true; }
        }
    }

    static void _updateCh(Ch& ch, volatile bool& valid, volatile float& norm) {
        if (ch.pin < 0) { valid = false; return; }

        uint32_t now = millis();

        if (ch.fresh) {
            ch.fresh  = false;
            ch.lastMs = now;
            int range = Config::rcMaxUs - Config::rcMinUs;
            if (range == 0) { valid = false; return; }  // misconfigured — guard div/0
            float n = (float)((int)ch.pulseUs - Config::rcMinUs) / (float)range;
            norm  = constrain(n, 0.0f, 1.0f);
            valid = true;
        } else if (valid && (now - ch.lastMs) > (uint32_t)Config::rcFailsafeMs) {
            valid = false;
            norm  = 0.0f;
        }
    }
};

// Static member definitions (header-only, so inline)
inline RCInput::Ch RCInput::_thr{};
inline RCInput::Ch RCInput::_idle{};
