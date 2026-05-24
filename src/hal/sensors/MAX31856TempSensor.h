#pragma once
#include "ISensor.h"
#include <Arduino.h>

// ============================================================
//  MAX31856TempSensor — multi-type thermocouple, direct SPI
//
//  Supports all thermocouple types: B, E, J, K, N, R, S, T
//  (pass the two-letter string as tcType, e.g. "K" or "J")
//
//  Datasheet SPI: CPOL=1, CPHA=1 (mode 1 or 3 — reads on
//  falling SCLK, MAX31856 is mode 1).  CS active LOW.
//
//  Register map (read addresses = reg | 0x00, write = reg | 0x80):
//    0x00  CR0  — configuration register 0 (noise filter, auto-conv)
//    0x01  CR1  — configuration register 1 (thermocouple type)
//    0x02  MASK — fault mask
//    0x0C  LTCBH — linearised TC temperature MSB  (19-bit + sign)
//    0x0D  LTCBM — linearised TC temperature mid
//    0x0E  LTCBL — linearised TC temperature LSB
//    0x0F  SR   — fault status register
//
//  Temperature encoding (registers 0x0C–0x0E):
//    Bits 31–13 (19-bit + sign, 0.0078125 °C / LSB)
//    Shift raw 32-bit read right by 13 bits, sign-extend.
// ============================================================

class MAX31856TempSensor : public ISensor {
public:
    // tcType: "B","E","J","K","N","R","S","T"  (case-insensitive first char)
    MAX31856TempSensor(int clkPin, int csPin, int misoPin, int mosiPin,
                       const char* tcType, const char* sensorName)
        : _clk(clkPin), _cs(csPin), _miso(misoPin), _mosi(mosiPin),
          _name(sensorName)
    {
        _tcReg = _typeToReg(tcType);
    }

    void begin() override {
        pinMode(_cs,   OUTPUT); digitalWrite(_cs,   HIGH);
        pinMode(_clk,  OUTPUT); digitalWrite(_clk,  HIGH);  // CPOL=1 idle high
        pinMode(_miso, INPUT);
        if (_mosi >= 0) { pinMode(_mosi, OUTPUT); digitalWrite(_mosi, LOW); }

        // CR0 = 0x81: 50 Hz noise filter + auto-convert mode (continuous)
        _writeReg(0x00, 0x81);
        // CR1 = tcType in lower nibble, averaging = 1 sample (bits 6:4 = 000)
        _writeReg(0x01, _tcReg & 0x0F);

        _temp    = 0;
        _healthy = false;
        _lastMs  = 0;
    }

    void update() override {
        unsigned long now = millis();
        if (now - _lastMs < READ_INTERVAL_MS) return;
        _lastMs = now;

        // Check fault status register first
        uint8_t sr = _readReg(0x0F);
        if (sr & 0x3F) {               // any fault bit set (bits 5:0)
            _healthy = false;
            return;
        }

        // Read linearised TC temperature — 3 bytes starting at 0x0C
        // MAX31856 auto-increments registers when CS held low
        uint32_t raw = _readReg3(0x0C);  // returns 24-bit value

        // Bits 23:5 = 19-bit + sign temperature, 0.0078125 °C/LSB
        // Bit 0 of last byte is the fault flag (redundant, already checked)
        // raw is left-justified in the 24-bit field: shift right by 5 → bits 18:0
        int32_t val = (int32_t)((raw & 0xFFFFE0u) >> 5);    // 19-bit signed
        if (val & 0x40000) val |= (int32_t)0xFFF80000;  // sign extend

        _temp = val * 0.0078125f;

        // Physical sanity check
        if (_temp < -210.0f || _temp > 1800.0f) {
            _healthy = false;
            return;
        }
        _healthy = true;
    }

    float       getValue()  override { return _temp; }
    bool        isHealthy() override { return _healthy; }
    const char* name()      override { return _name; }

private:
    static constexpr unsigned long READ_INTERVAL_MS = 100;

    int         _clk, _cs, _miso, _mosi;
    const char* _name;
    uint8_t     _tcReg   = 0x03;   // default K-type
    float       _temp    = 0;
    bool        _healthy = false;
    unsigned long _lastMs = 0;

    // CR1 lower nibble codes per datasheet Table 4
    static uint8_t _typeToReg(const char* t) {
        if (!t || !t[0]) return 0x03; // default K
        switch (t[0] | 0x20) {        // to lower
            case 'b': return 0x00;
            case 'e': return 0x01;
            case 'j': return 0x02;
            case 'k': return 0x03;
            case 'n': return 0x04;
            case 'r': return 0x05;
            case 's': return 0x06;
            case 't': return 0x07;
            default:  return 0x03;
        }
    }

    // Software SPI helpers — MAX31856 is SPI mode 1 (CPOL=0,CPHA=1)
    // We use CPOL=1 (idle high) which is compatible because the chip
    // samples on the falling edge of SCLK in both mode 1 and 3.
    uint8_t _transferByte(uint8_t out) {
        uint8_t in = 0;
        for (int i = 7; i >= 0; i--) {
            if (_mosi >= 0) digitalWrite(_mosi, (out >> i) & 1);
            digitalWrite(_clk, LOW);
            delayMicroseconds(1);
            if (digitalRead(_miso)) in |= (1 << i);
            digitalWrite(_clk, HIGH);
            delayMicroseconds(1);
        }
        return in;
    }

    void _writeReg(uint8_t reg, uint8_t val) {
        digitalWrite(_cs, LOW);
        delayMicroseconds(1);
        _transferByte(reg | 0x80);   // write address
        _transferByte(val);
        digitalWrite(_cs, HIGH);
        delayMicroseconds(1);
    }

    uint8_t _readReg(uint8_t reg) {
        digitalWrite(_cs, LOW);
        delayMicroseconds(1);
        _transferByte(reg & 0x7F);   // read address
        uint8_t val = _transferByte(0x00);
        digitalWrite(_cs, HIGH);
        return val;
    }

    // Read 3 consecutive registers into a 24-bit value (MSB first)
    uint32_t _readReg3(uint8_t startReg) {
        digitalWrite(_cs, LOW);
        delayMicroseconds(1);
        _transferByte(startReg & 0x7F);
        uint32_t val  = (uint32_t)_transferByte(0) << 16;
        val |= (uint32_t)_transferByte(0) << 8;
        val |= (uint32_t)_transferByte(0);
        digitalWrite(_cs, HIGH);
        return val;
    }
};
