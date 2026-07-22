#pragma once
#include <stdint.h>
#include <stddef.h>

// Pure protocol decoders mirrored by the hardware drivers and usable on a host.
namespace SensorProtocolDecode {
constexpr bool max31855(uint32_t raw, float& celsius) {
    if (raw == 0xFFFFFFFFu || (raw & 0x00010000u)) return false;
    int16_t value = (int16_t)(raw >> 18);
    if (value & 0x2000) value |= (int16_t)0xC000;
    celsius = value * 0.25f;
    return celsius >= -200.0f && celsius <= 1350.0f;
}
constexpr bool max31856(uint32_t raw24, uint8_t status, float& celsius) {
    if (status != 0) return false;
    int32_t value = (int32_t)((raw24 & 0xFFFFE0u) >> 5);
    if (value & 0x40000) value |= (int32_t)0xFFF80000;
    celsius = value * 0.0078125f;
    return celsius >= -210.0f && celsius <= 1800.0f;
}
constexpr bool max6675(uint16_t raw, float& celsius) {
    if (raw & 0x0004u) return false;
    celsius = ((raw >> 3) & 0x0FFFu) * 0.25f;
    return celsius >= 0.0f && celsius <= 1023.75f;
}
constexpr bool hx711(uint32_t raw24, int32_t& counts) {
    raw24 &= 0xFFFFFFu;
    if (raw24 == 0x7FFFFFu || raw24 == 0x800000u) return false;
    counts = (raw24 & 0x800000u) ? (int32_t)(raw24 | 0xFF000000u) : (int32_t)raw24;
    return true;
}
constexpr uint8_t dallasCrc8(const uint8_t* data, size_t length) {
    uint8_t crc = 0;
    while (length--) {
        uint8_t in = *data++;
        for (uint8_t i = 0; i < 8; ++i) {
            const uint8_t mix = (crc ^ in) & 1u;
            crc >>= 1;
            if (mix) crc ^= 0x8Cu;
            in >>= 1;
        }
    }
    return crc;
}
constexpr bool ds18b20(const uint8_t scratch[9], uint8_t resolution, float& celsius) {
    if (!scratch || dallasCrc8(scratch, 8) != scratch[8]) return false;
    int16_t raw = (int16_t)(((uint16_t)scratch[1] << 8) | scratch[0]);
    if (resolution == 9) raw &= ~0x07;
    else if (resolution == 10) raw &= ~0x03;
    else if (resolution == 11) raw &= ~0x01;
    celsius = raw * 0.0625f;
    return celsius >= -55.0f && celsius <= 125.0f;
}

constexpr bool selfTest() {
    float t = 0.0f;
    int32_t counts = 0;
    uint8_t scratch[9] = {0x50,0x05,0x4B,0x46,0x7F,0xFF,0x0C,0x10,0};
    scratch[8] = dallasCrc8(scratch, 8);
    return max31855(0, t) && t == 0.0f &&
           !max31855(0xFFFFFFFFu, t) &&
           max31856((uint32_t)104384 << 5, 0, t) && t == 815.5f &&
           max6675((uint16_t)(2569u << 3), t) && t == 642.25f &&
           hx711(0xFFFF00u, counts) && counts == -256 &&
           !hx711(0x7FFFFFu, counts) &&
           ds18b20(scratch, 12, t) && t == 85.0f;
}
static_assert(selfTest(), "sensor protocol decode vectors failed");
}
