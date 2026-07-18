#pragma once

#include <atomic>
#include <stdint.h>

// Serialises live configuration replacement against the final START
// transition. The web task writes Config statics on Core 0; the ECU task
// consumes and applies them on Core 1. A single atomic state closes both
// directions of the old STANDBY-check/START race.
class ConfigApplyGate {
public:
    enum State : uint8_t { Idle, WebWriting, ReadyForCore, CoreApplying, StartTransition };

    static bool tryBeginWebWrite() {
        uint8_t expected = Idle;
        return _state.compare_exchange_strong(expected, WebWriting, std::memory_order_acq_rel);
    }

    static void markReadyForCore() {
        _state.store(ReadyForCore, std::memory_order_release);
    }

    static bool tryBeginCoreApply() {
        uint8_t expected = ReadyForCore;
        return _state.compare_exchange_strong(expected, CoreApplying, std::memory_order_acq_rel);
    }

    static bool tryBeginStartTransition() {
        uint8_t expected = Idle;
        return _state.compare_exchange_strong(expected, StartTransition, std::memory_order_acq_rel);
    }

    static void release() { _state.store(Idle, std::memory_order_release); }
    static bool busy() { return _state.load(std::memory_order_acquire) != Idle; }
    static State state() { return static_cast<State>(_state.load(std::memory_order_acquire)); }

private:
    static inline std::atomic<uint8_t> _state{Idle};
};
