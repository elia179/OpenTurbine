#include "../src/hal/sensors/SensorProtocolDecode.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

static bool near(float a, float b, float eps = 0.001f) { return std::fabs(a - b) <= eps; }
int main() {
    float t = 999.0f;
    assert(SensorProtocolDecode::max31855(0, t) && near(t, 0));
    assert(SensorProtocolDecode::max31855((uint32_t)(400 * 4) << 18, t) && near(t, 400));
    assert(!SensorProtocolDecode::max31855(0x00010001u, t));
    assert(!SensorProtocolDecode::max31855(0xFFFFFFFFu, t));
    const int32_t m56 = (int32_t)(815.5f / 0.0078125f);
    assert(SensorProtocolDecode::max31856((uint32_t)m56 << 5, 0, t) && near(t, 815.5f));
    assert(!SensorProtocolDecode::max31856(0, 1, t));
    assert(SensorProtocolDecode::max6675((uint16_t)((642.25f / 0.25f) * 8), t) && near(t, 642.25f));
    assert(!SensorProtocolDecode::max6675(4, t));
    int32_t counts = 0;
    assert(SensorProtocolDecode::hx711(0x000123u, counts) && counts == 0x123);
    assert(SensorProtocolDecode::hx711(0xFFFF00u, counts) && counts == -256);
    assert(!SensorProtocolDecode::hx711(0x7FFFFFu, counts));
    assert(!SensorProtocolDecode::hx711(0x800000u, counts));
    uint8_t scratch[9] = {0x50,0x05,0x4B,0x46,0x7F,0xFF,0x0C,0x10,0};
    scratch[8] = SensorProtocolDecode::dallasCrc8(scratch, 8);
    assert(SensorProtocolDecode::ds18b20(scratch, 12, t) && near(t, 85));
    scratch[8] ^= 1;
    assert(!SensorProtocolDecode::ds18b20(scratch, 12, t));
    std::cout << "sensor protocol vectors passed (14 checks)\n";
}
