#pragma once
#include "../IBlock.h"
#include <Arduino.h>

// General-purpose fixed time delay between any two sequence steps.
// dwellMs is configurable at runtime via Config (sequence.startup.timed_delay_ms).
// No actuator changes — just waits.
class TimedDelay : public IBlock {
public:
    unsigned long dwellMs = 1000;

    const char* name() override { return "TimedDelay"; }

    void onEnter() override {
        _entryMs = millis();
    }

    BlockResult tick() override {
        if ((millis() - _entryMs) >= dwellMs) return BlockResult::Complete;
        return BlockResult::Running;
    }

    void onExit() override {}

private:
    unsigned long _entryMs = 0;
};
