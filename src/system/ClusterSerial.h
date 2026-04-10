#pragma once
#include "../engine/EngineData.h"
#include "../engine/Types.h"
#include <HardwareSerial.h>
#include <stdint.h>

// Default baud rate — must match ECU_BAUD in the cluster's KicConfig.h.
// Override by defining OT_CLUSTER_BAUD before this header (e.g. in hardware_profile.h).
// Default is 115200 to match common cluster hardware and USB serial monitors.
// High-speed clusters that support it can define OT_CLUSTER_BAUD 460800UL in hardware_profile.h.
#ifndef OT_CLUSTER_BAUD
#define OT_CLUSTER_BAUD 115200UL
#endif

// ============================================================
//  ClusterSerial — instrument cluster output (TX-only UART)
//
//  Enabled via OT_HAS_CLUSTER_SERIAL in hardware_profile.h.
//
//  Protocol v2 — schema-first, positional runtime data:
//
//  Boot (sent 3× so cluster doesn't miss it while still booting):
//    OT:<ver>\n            — protocol version marker (must be first)
//    P:<profile>\n         — profile identifier
//    M:<code>,<sev>,<label>\n  — message table (sev: 0=info 1=warn 2=crit)
//    F:<idx>,<key>,<type>,<unit>\n — field definitions (idx = D: column)
//    L:<key>=<val>;...\n   — gauge limits / warning thresholds
//    Z\n                   — end of schema
//    S:<code>\n            — initial status
//
//  Runtime:
//    D:<v0>,<v1>,...\n     — positional sensor values (no keys, see F: defs)
//    S:<code>\n            — status change (mode transition or block event)
// ============================================================

// Status codes — must match M: message table entries
namespace ClCode {
    static constexpr uint8_t Running          =  1;
    static constexpr uint8_t RelightActive    =  2;
    static constexpr uint8_t StartingUp       =  3;
    static constexpr uint8_t ReadyToStart     =  4;
    static constexpr uint8_t Igniting         =  5;
    static constexpr uint8_t Ignited          =  6;
    static constexpr uint8_t IgnitionFailed   =  7;
    static constexpr uint8_t WaitingN1Rise    =  8;
    static constexpr uint8_t FlameOut         =  9;
    static constexpr uint8_t ShuttingDown     = 10;
    static constexpr uint8_t CooldownRunning  = 11;
    static constexpr uint8_t OilCalFailed     = 12;
    static constexpr uint8_t StopSwitchActive = 13;
    static constexpr uint8_t OilPressureLow   = 14;
    static constexpr uint8_t Overspeed        = 15;
    static constexpr uint8_t OilZero          = 16;  // oil near-zero / disconnected fitting
    static constexpr uint8_t TotHigh          = 17;  // TOT above warning threshold (not yet limit)
    static constexpr uint8_t OilWarn          = 18;  // oil approaching low warning (not yet fault)
}

class ClusterSerial {
public:
    // Call once from setup() — sends full boot schema + initial status
    static void begin();

    // Call every ECU loop tick — sends periodic data and mode-change status codes
    static void tick();

    // Send a specific status code immediately (e.g. from fault handlers in main.cpp)
    static void sendStatus(uint8_t code);

private:
    static unsigned long _lastDataMs;
    static SysMode       _lastMode;
    static uint8_t       _lastClusterCode;  // last block-level code sent
    static bool          _totWarnActive;    // true while TOT is above warn threshold
    static bool          _oilWarnActive;    // true while oil is below warn threshold
};
