#pragma once

class IController {
public:
    virtual ~IController() = default;
    virtual void begin()  = 0;
    virtual void tick()   = 0;  // called every loop tick in RUNNING (and partly in STARTUP)
    virtual void reset()  = 0;  // reset internal state (called on mode transitions)
};
