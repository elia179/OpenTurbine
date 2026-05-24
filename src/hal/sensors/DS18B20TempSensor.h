#pragma once
#include "ISensor.h"
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <new>

// ============================================================
//  DS18B20TempSensor — Dallas/Maxim DS18B20 1-Wire digital thermometer
//
//  Single-pin OneWire interface; auto-discovers the first device
//  on the bus.  Supports 9–12-bit resolution (default 12-bit).
//
//  Conversion is fully asynchronous — update() triggers the next
//  conversion at the end of each read so the ECU loop never blocks.
//
//  Conversion times:
//    9-bit  →  94 ms   (0.5 °C resolution)
//   10-bit  → 188 ms   (0.25 °C)
//   11-bit  → 375 ms   (0.125 °C)
//   12-bit  → 750 ms   (0.0625 °C)
//
//  Healthy range: −55 °C to +125 °C (sensor spec).
//  85 °C returned on power-on before first conversion — treated as fault.
//  −127 °C / DEVICE_DISCONNECTED_C returned when no device found — fault.
//
//  Uses placement-new (no heap allocation after begin()).
// ============================================================

class DS18B20TempSensor : public ISensor {
public:
    explicit DS18B20TempSensor(const char* sensorName)
        : _pin(-1), _resolution(12), _name(sensorName),
          _ow(nullptr), _dt(nullptr) {}

    // Runtime init — call once at boot (or on pin/resolution change).
    void begin(int pin, uint8_t resolution = 12) {
        _pin        = (int8_t)pin;
        _resolution = (uint8_t)constrain((int)resolution, 9, 12);

        // Placement-new: destruct any previous instance first.
        if (_ow) { _ow->~OneWire(); }
        _ow = new (_owBuf) OneWire(_pin);

        if (_dt) { _dt->~DallasTemperature(); }
        _dt = new (_dtBuf) DallasTemperature(_ow);

        _dt->begin();
        _numDevices = _dt->getDeviceCount();

        if (_numDevices > 0) {
            _dt->setResolution(_resolution);
            _dt->setWaitForConversion(false);   // non-blocking requestTemperatures()
            _dt->requestTemperatures();          // kick off first conversion
            _convReadyMs = millis() + _convDelayMs();
            _convPending = true;
        } else {
            _convPending = false;
        }

        _temp    = 0.0f;
        _healthy = false;
    }

    // ISensor::begin() — re-init with current pin and resolution.
    void begin() override { begin(_pin, _resolution); }

    void update() override {
        if (!_dt || _numDevices == 0) return;

        unsigned long now = millis();

        if (!_convPending) {
            // Safety: restart conversion if somehow never started.
            _dt->requestTemperatures();
            _convReadyMs = now + _convDelayMs();
            _convPending = true;
            return;
        }

        if (now < _convReadyMs) return;   // conversion still in progress

        float t = _dt->getTempCByIndex(0);
        // Reject known fault values:
        //   DEVICE_DISCONNECTED_C = -127  → no device
        //   85.0 °C               → power-on reset, not yet converted
        if (t > -100.0f && fabsf(t - 85.0f) > 0.01f && t < 130.0f) {
            _temp    = t;
            _healthy = true;
        } else {
            _healthy = false;
        }

        // Request the next conversion immediately.
        _dt->requestTemperatures();
        _convReadyMs = now + _convDelayMs();
        // _convPending stays true
    }

    float       getValue()  override { return _temp; }
    bool        isHealthy() override { return _healthy && _numDevices > 0; }
    const char* name()      override { return _name; }

private:
    unsigned long _convDelayMs() const {
        switch (_resolution) {
            case 9:  return  94UL;
            case 10: return 188UL;
            case 11: return 375UL;
            default: return 750UL;   // 12-bit
        }
    }

    int8_t      _pin;
    uint8_t     _resolution;
    const char* _name;
    int         _numDevices  = 0;

    // Placement-new storage — avoids heap allocation.
    alignas(OneWire)           uint8_t _owBuf[sizeof(OneWire)];
    alignas(DallasTemperature) uint8_t _dtBuf[sizeof(DallasTemperature)];
    OneWire*           _ow;
    DallasTemperature* _dt;

    float         _temp         = 0.0f;
    bool          _healthy      = false;
    unsigned long _convReadyMs  = 0;
    bool          _convPending  = false;
};
