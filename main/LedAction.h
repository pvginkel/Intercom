#pragma once

#include "Callback.h"

enum class LedState { On, Off, Blink };

struct LedAction {
    LedState state;
    int duration;
    int on;
    int off;
};

class LedActionRunner {
    LedAction* _action;
    Callback<bool> _led_changed;

public:
    LedActionRunner(LedAction* action) : _action(action) {}
    virtual ~LedActionRunner() { delete _action; }

    void on_led_changed(function<void(bool)> func) { _led_changed.add(func); }
    virtual bool update() = 0;

protected:
    LedAction* action() { return _action; }
    void set_led_state(bool state) { _led_changed.call(state); }
};

class LedOnActionRunner : public LedActionRunner {
    time_t _delay_until{};

public:
    LedOnActionRunner(LedAction* action) : LedActionRunner(action) {}

    bool update();
};

class LedOffActionRunner : public LedActionRunner {
public:
    LedOffActionRunner(LedAction* action) : LedActionRunner(action) {}

    bool update();
};

class LedBlinkActionRunner : public LedActionRunner {
    time_t _max_runtime{};
    time_t _delay_until{};
    bool _next_state{};

public:
    LedBlinkActionRunner(LedAction* action);

    bool update();
};
