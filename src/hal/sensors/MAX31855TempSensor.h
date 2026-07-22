#pragma once
#include "ISensor.h"
#include <SPI.h>
#include <Arduino.h>

// ============================================================
//  MAX31855TempSensor — K-type thermocouple, direct SPI
//
//  No Adafruit library — reads 32 bits directly via SPI.
//  Bit layout (per datasheet):
//    31–18  Thermocouple temp (13-bit + sign, 0.25°C/LSB)
//    16     Fault bit (1 = any fault present)
//    3      SCV: short to VCC
//    2      SCG: short to GND
//    1      OC:  open circuit
// ============================================================

class MAX31855TempSensor : public ISensor {
public:
    MAX31855TempSensor(int clkPin, int csPin, int misoPin, const char* sensorName)
        : _clk(clkPin), _cs(csPin), _miso(misoPin), _name(sensorName) {}

    void begin(int clk, int cs, int miso) {
        _clk = clk; _cs = cs; _miso = miso;
        begin();
    }

    void begin() override {
        pinMode(_cs, OUTPUT);
        digitalWrite(_cs, HIGH);
        // Software SPI — no conflicts with other SPI devices or bus sharing issues
        pinMode(_clk,  OUTPUT);
        digitalWrite(_clk, LOW);
        // A real converter overrides this weak pull-up; without a converter,
        // it prevents floating SPI data from appearing as hot exhaust.
        pinMode(_miso, INPUT_PULLUP);
        _temp    = 0;
        _healthy = false;
        _lastMs  = 0;
        _sampleSeq = 0;
    }

    void update() override {
        unsigned long now = millis();
        if (now - _lastMs < READ_INTERVAL_MS) return;
        _lastMs = now;

        uint32_t raw = _read32();

        if (raw == 0xFFFFFFFFUL) {
            _healthy = false;
            return;
        }

        // Fault bits — bit 16 is the summary fault flag
        if (raw & 0x00010000UL) {
            _healthy = false;
            return;
        }

        // Bits 31–18: thermocouple temperature (14-bit two's complement, 0.25°C LSB)
        int16_t tc = (int16_t)(raw >> 18);
        // Sign-extend 14-bit value to 16-bit
        if (tc & 0x2000) tc |= 0xC000;

        float temp = tc * 0.25f;
        // MAX31855 physical range: -200 to +1350 °C
        // Values outside this range indicate hardware / wiring fault
        if (temp < -200.0f || temp > 1350.0f) {
            _healthy = false;
            return;
        }
        _temp = temp;
        _healthy = true;
        ++_sampleSeq;
    }

    float       getValue()  override { return _temp; }
    bool        isHealthy() override { return _healthy; }
    const char* name()      override { return _name; }
    uint32_t sampleSequence() override { return _sampleSeq; }
    uint32_t sampleTimestampMs() override { return _lastMs; }

private:
    static constexpr unsigned long READ_INTERVAL_MS = 100;
    // A conservative software-SPI clock is effectively free at a 10 Hz
    // conversion rate and gives isolators, long hobby wiring, and slower GPIO
    // targets time to settle before each sample.
    static constexpr unsigned int SPI_HALF_PERIOD_US = 5;

    int         _clk, _cs, _miso;
    const char* _name;
    float       _temp    = 0;
    bool        _healthy = false;
    unsigned long _lastMs = 0;
    uint32_t      _sampleSeq = 0;

    uint32_t _read32() {
        uint32_t val = 0;
        digitalWrite(_cs, LOW);
        delayMicroseconds(1);
        for (int i = 31; i >= 0; i--) {
            digitalWrite(_clk, LOW);
            delayMicroseconds(SPI_HALF_PERIOD_US);
            if (digitalRead(_miso)) val |= (1UL << i);
            digitalWrite(_clk, HIGH);
            delayMicroseconds(SPI_HALF_PERIOD_US);
        }
        digitalWrite(_cs, HIGH);
        return val;
    }
};
