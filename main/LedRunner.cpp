#include "support.h"

#include "LedRunner.h"

LOG_TAG(LedRunner);

bool LedOnRunner::update() {
    if (_delay_until && esp_get_millis() >= _delay_until) {
        set_led_level(0);
        return false;
    }

    set_led_level(1);

    if (_duration <= 0) {
        return false;
    }

    _delay_until = esp_get_millis() + _duration;

    return true;
}

bool LedOffRunner::update() {
    set_led_level(0);

    return false;
}

LedBlinkRunner::LedBlinkRunner(time_t duration, time_t on, time_t off) : _on(on), _off(off) {
    if (duration > 0) {
        _max_runtime = esp_get_millis() + duration;
    } else {
        _max_runtime = 0;
    }

    _next_state = true;
}

bool LedBlinkRunner::update() {
    const auto millis = esp_get_millis();

    if (_max_runtime && millis >= _max_runtime) {
        return false;
    }

    if (millis >= _delay_until) {
        set_led_level(_next_state ? 1 : 0);

        _delay_until = millis + (_next_state ? _on : _off);
        _next_state = !_next_state;
    }

    return true;
}

LedFadeRunner::LedFadeRunner(float start_level, time_t duration, time_t interval)
    : _level(start_level), _interval(interval) {
    const auto millis = esp_get_millis();

    _last_update = millis;

    if (duration > 0) {
        _max_runtime = millis + duration;
    } else {
        _max_runtime = 0;
    }
}

bool LedFadeRunner::update() {
    const auto millis = esp_get_millis();

    if (_max_runtime && millis >= _max_runtime) {
        set_led_level(0);
        return false;
    }

    const auto elapsed = millis - _last_update;
    _last_update = millis;

    const auto step = float(elapsed) / float(_interval);

    if (_going_up) {
        _level = _level + step;
        if (_level > 1.0f) {
            _level = 2.0f - _level;
            _going_up = false;
        }
    } else {
        _level = _level - step;
        if (_level < 0.0f) {
            _level = -_level;
            _going_up = true;
        }
    }

    set_led_level(_level);

    return true;
}
