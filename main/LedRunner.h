#pragma once

#include "Callback.h"

class LedRunner {
    Callback<float> _led_changed;

public:
    virtual ~LedRunner() {}

    void on_led_changed(function<void(float)> func) { _led_changed.add(func); }
    virtual bool update() = 0;

protected:
    void set_led_level(float level) { _led_changed.call(level); }
};

class LedOnRunner : public LedRunner {
    time_t _duration;
    time_t _delay_until{};

public:
    LedOnRunner(time_t duration) : _duration(duration) {}

    bool update();
};

class LedOffRunner : public LedRunner {
public:
    bool update();
};

class LedBlinkRunner : public LedRunner {
    time_t _on;
    time_t _off;
    time_t _max_runtime;
    time_t _delay_until{};
    bool _next_state{};

public:
    LedBlinkRunner(time_t duration, time_t on, time_t off);

    bool update();
};

class LedFadeRunner : public LedRunner {
    float _level;
    time_t _max_runtime;
    time_t _interval;
    time_t _last_update;
    bool _going_up{true};

public:
    LedFadeRunner(float start_level, time_t duration, time_t interval);

    bool update();
};
