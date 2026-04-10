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
        pcnt_unit_config_t unitCfg = {
            .low_limit  = -1,
            .high_limit = H_LIM,
            .intr_priority = 0,
            .flags = { .accum_count = 1 }  // auto-accumulate on overflow
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
        _rpm       = 0;
        _health.clear();
    }

    void update() override {
        unsigned long now = millis();
        unsigned long dt  = now - _lastMs;
        if (dt < UPDATE_INTERVAL_MS) return;

        int raw = 0;
        pcnt_unit_get_count(_unit, &raw);

        int delta = raw - _lastCount;
        if (delta < 0) delta += H_LIM;   // handle counter wrap

        float newRpm = (delta / _ppr) * (60000.0f / dt);

        _updateHealth(newRpm, delta, dt);

        _rpm       = newRpm;
        _lastMs    = now;
        _lastCount = raw;
    }

    float       getValue()  override { return _rpm; }
    bool        isHealthy() override { return !_health.any(); }
    const char* name()      override { return _name; }

    void resetHealth() {
        _health.clear();
        _zeroCount = 0;
        _prevRpm   = 0;
    }

    // Configurable health thresholds — set from applyConfig() via Hardware.h
    float jumpThreshold  = 0.40f;  // fraction: 0.40 = 40% relative RPM jump → fault
    int   zeroStuckLimit = 5;      // ticks at UPDATE_INTERVAL_MS before ZERO_STUCK fault

private:
    static constexpr int           H_LIM              = 30000;
    static constexpr unsigned long UPDATE_INTERVAL_MS = 100;

    int         _pin;
    float       _ppr;
    const char* _name;

    pcnt_unit_handle_t _unit     = nullptr;
    unsigned long      _lastMs   = 0;
    int                _lastCount= 0;
    float              _rpm      = 0;
    float              _prevRpm  = 0;
    int                _zeroCount= 0;
    RpmHealth          _health;

    void _updateHealth(float rpm, int delta, unsigned long dt) {
        _health.clear();

        // Saturated: pulse counter not advancing even though RPM was non-zero.
        // The old check (rpm > 0 && _lastCount == 0) was impossible — if _lastCount
        // is 0 there are no pulses, so rpm would also be 0.  The real fault is a
        // sensor stuck high (hardware oscillating) or suddenly silent after running.
        if (_prevRpm > 500.0f && delta == 0) {
            _health.set(RpmHealth::SATURATED);
        }

        // Implausible jump: use absolute RPM/s rate limit rather than a relative
        // fraction so false positives at low RPM are avoided.
        // Default jumpThreshold (0.40) now means: max 0.40 × 60000 = 24000 RPM/s.
        // Reinterpret: jumpThreshold as fraction of rpmLimit (set to e.g. 100000)
        // per UPDATE_INTERVAL_MS — adjust jumpThreshold in hardware_profile.h if needed.
        if (_prevRpm > 500.0f && rpm > 0) {
            float maxDeltaRpm = jumpThreshold * 60000.0f * (dt / 1000.0f); // RPM limit per interval
            if (fabsf(rpm - _prevRpm) > maxDeltaRpm) {
                _health.set(RpmHealth::JUMP);
            }
        }

        // Zero-stuck: engine clearly running but reading zero
        if (_prevRpm > 2000.0f && rpm < 1.0f) {
            if (++_zeroCount >= zeroStuckLimit) {
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
