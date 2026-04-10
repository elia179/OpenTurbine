#pragma once
#include <stdint.h>

// ── Engine operating modes ────────────────────────────────────
enum class SysMode : uint8_t {
    STANDBY,   // idle, ready to start
    STARTUP,   // startup sequence running
    RUNNING,   // engine at speed, safety monitors active
    SHUTDOWN,  // shutdown sequence running
    FAULT      // halted — profile ID mismatch or unrecoverable error
};

inline const char* sysModeStr(SysMode m) {
    switch (m) {
        case SysMode::STANDBY:  return "STANDBY";
        case SysMode::STARTUP:  return "STARTUP";
        case SysMode::RUNNING:  return "RUNNING";
        case SysMode::SHUTDOWN: return "SHUTDOWN";
        case SysMode::FAULT:    return "FAULT";
    }
    return "UNKNOWN";
}

// ── RPM health fault bitmask ──────────────────────────────────
struct RpmHealth {
    static constexpr uint8_t OK           = 0x00;
    static constexpr uint8_t SATURATED    = 0x01; // reading stuck at max
    static constexpr uint8_t JUMP         = 0x02; // implausible step change
    static constexpr uint8_t ZERO_STUCK   = 0x04; // zero for too many ticks
    static constexpr uint8_t ZERO_GLITCH  = 0x08; // brief zero during run

    uint8_t faults = OK;

    void clear()              { faults = OK; }
    void set(uint8_t f)       { faults |= f; }
    bool any() const          { return faults != OK; }
    bool isTrustworthy() const { return (faults & ~ZERO_GLITCH) == OK; }
};
