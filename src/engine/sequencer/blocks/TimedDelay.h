#pragma once
#include "../IBlock.h"
#include <Arduino.h>

// General-purpose time delay between any two sequence steps.
// Each placed block receives its own dwellMs from the stored sequence slot.
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
