#pragma once
#include "ISensor.h"
#include "driver/pulse_cnt.h"
#include "../../engine/Types.h"
#include <Arduino.h>

// ============================================================
//  PCNTRpmSensor — shaft RPM using ESP32 hardware pulse counter
//
//  Uses the IDF5 PCNT unit API (driver/pulse_cnt.h).
//  Hardware counting is interrupt-free and glitch-filtered.
//  RPM computed from count delta every UPDATE_INTERVAL_MS.
//  Reports RpmHealth fault bits (SATURATED / JUMP / ZERO_STUCK).
//  Brief ZERO_GLITCH samples are tolerated (RpmHealth::isTrustworthy):
//  the last real reading is held instead of publishing a healthy 0 RPM.
// ============================================================

class PCNTRpmSensor : public ISensor {
public:
    PCNTRpmSensor(int pin, float pulsesPerRev, const char* sensorName)
        : _pin(pin), _ppr(pulsesPerRev), _name(sensorName) {}

    // Runtime-pin overload: update pin/ppr then initialise hardware.
    // Called by Hardware::initSensors() when runtime config is loaded.
    void begin(int pin, float ppr) {
        _pin = pin;
        _ppr = ppr;
        begin();
    }

    void begin() override {
        if (_ppr <= 0.0f) {
            Serial.printf("[%s] Invalid pulses/rev %.3f; using 1.0\n", _name, (double)_ppr);
            _ppr = 1.0f;
        }
        pcnt_unit_config_t unitCfg = {
            .low_limit  = -1,
            .high_limit = H_LIM,
            .intr_priority = 0,
            // accum_count=0: we track wraps manually in int64_t so the
            // accumulated total never overflows (the IDF's own software
            // accumulator is int32, which wraps after ~14 days at max RPM).
            .flags = { .accum_count = 0 }
        };
        ESP_ERROR_CHECK(pcnt_new_unit(&unitCfg, &_unit));

        // Glitch filter: ignore pulses shorter than 1000 ns (1 µs)
        pcnt_glitch_filter_config_t flt = { .max_glitch_ns = 1000 };
        ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(_unit, &flt));

        pcnt_chan_config_t chanCfg = {
            .edge_gpio_num  = _pin,
            .level_gpio_num = -1,  // not used (IDF5 — no PCNT_PIN_NOT_USED macro)
            .flags = {}
        };
        pcnt_channel_handle_t chan = nullptr;
        ESP_ERROR_CHECK(pcnt_new_channel(_unit, &chanCfg, &chan));

        // Count on rising edge only
        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE,   // rising  → +1
            PCNT_CHANNEL_EDGE_ACTION_HOLD));      // falling → no change

        ESP_ERROR_CHECK(pcnt_unit_enable(_unit));
        ESP_ERROR_CHECK(pcnt_unit_clear_count(_unit));
        ESP_ERROR_CHECK(pcnt_unit_start(_unit));

        _lastMs    = millis();
        _lastCount = 0;
        _accumPulses = 0;
        _rpm       = 0;
        _health.clear();
    }

    void update() override {
        unsigned long now = millis();
        unsigned long dt  = now - _lastMs;
        if (dt < UPDATE_INTERVAL_MS) return;

        // Read the raw hardware counter (bounded 0…H_LIM-1).
        // We use accum_count=0 and accumulate ourselves in int64_t so the
        // total never wraps — the IDF's own int32 accumulator overflows
        // after ~14 days at max RPM with typical PPR values.
        int rawInt = 0;
        pcnt_unit_get_count(_unit, &rawInt);

        int64_t delta = (int64_t)rawInt - _lastCount;
        if (delta < 0) delta += H_LIM;   // hardware counter wrapped 0…H_LIM
        _accumPulses += delta;

        float newRpm = ((float)delta / _ppr) * (60000.0f / (float)dt);

        _updateHealth(newRpm, (int)delta, dt);

        // ZERO_GLITCH is tolerable (Types.h isTrustworthy): hold the last real
        // reading for the few glitch ticks instead of publishing a healthy
        // 0 RPM, which would trip UNDERSPEED / latch limp mode instantly.
        if (_health.faults & RpmHealth::ZERO_GLITCH) {
            newRpm = _holdRpm;
        } else if (newRpm >= 1.0f) {
            _holdRpm = newRpm;
        }
        _rpm       = newRpm;
        _lastMs    = now;
        _lastCount = (int64_t)rawInt;
    }

    float       getValue()  override { return _rpm; }
    // ZERO_GLITCH alone does not fail health — see RpmHealth::isTrustworthy().
    bool        isHealthy() override { return _health.isTrustworthy(); }
    const char* name()      override { return _name; }

    void resetHealth() {
        _health.clear();
        _zeroCount = 0;
        _prevRpm   = 0;
        _holdRpm   = 0;
    }

    // Configurable health thresholds — set from applyConfig() via Hardware.h
    float jumpThreshold  = 0.40f;  // fraction of rpmLimit per second → JUMP fault
    int   zeroStuckLimit = 5;      // ticks at UPDATE_INTERVAL_MS before ZERO_STUCK fault
    float rpmLimit       = 60000.0f; // engine RPM limit — used to scale jumpThreshold

private:
    static constexpr int           H_LIM              = 30000;
    static constexpr unsigned long UPDATE_INTERVAL_MS = 100;

    int         _pin;
    float       _ppr;
    const char* _name;

    pcnt_unit_handle_t _unit        = nullptr;
    unsigned long      _lastMs      = 0;
    int64_t            _lastCount   = 0;   // last raw hardware counter value (0…H_LIM)
    int64_t            _accumPulses = 0;   // total pulses since begin() — never overflows
    float              _rpm         = 0;
    float              _prevRpm  = 0;
    float              _holdRpm  = 0;   // last real (non-glitch) reading, held during ZERO_GLITCH
    int                _zeroCount= 0;
    RpmHealth          _health;

    void _updateHealth(float rpm, int delta, unsigned long dt) {
        _health.clear();

        // Saturated: pulse counter not advancing even though RPM was non-zero.
        // The old check (rpm > 0 && _lastCount == 0) was impossible — if _lastCount
        // is 0 there are no pulses, so rpm would also be 0.  The real fault is a
        // sensor stuck high (hardware oscillating) or suddenly silent after running.
        int requiredZeros = zeroStuckLimit > 0 ? zeroStuckLimit : 1;
        if (_prevRpm > 2000.0f && delta == 0 && (_zeroCount + 1) >= requiredZeros) {
            _health.set(RpmHealth::SATURATED);
        }

        // Implausible jump: max allowed RPM/s = jumpThreshold × rpmLimit.
        // e.g. jumpThreshold=0.40, rpmLimit=100000 → 40000 RPM/s max rate.
        if (_prevRpm > 500.0f && rpm > 0) {
            float maxDeltaRpm = jumpThreshold * rpmLimit * (dt / 1000.0f);
            if (fabsf(rpm - _prevRpm) > maxDeltaRpm) {
                _health.set(RpmHealth::JUMP);
            }
        }

        // Zero-stuck: engine clearly running but reading zero.
        // The streak continues while the reading stays zero (_zeroCount > 0):
        // _prevRpm alone is 0 from the second zero tick onward and would
        // reset the counter, making ZERO_STUCK unreachable.
        if (rpm < 1.0f && (_prevRpm > 2000.0f || _zeroCount > 0)) {
            if (++_zeroCount >= requiredZeros) {
                _health.set(RpmHealth::ZERO_STUCK);
            } else {
                _health.set(RpmHealth::ZERO_GLITCH);
            }
        } else {
            _zeroCount = 0;
        }

        _prevRpm = rpm;
    }
};
