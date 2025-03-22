#include "includes.h"

#include "LedAction.h"

LOG_TAG(LedAction);

bool LedOnActionRunner::update() {
    if (_delay_until && esp_get_millis() >= _delay_until) {
        set_led_state(false);
        return false;
    }

    set_led_state(true);

    if (action()->duration <= 0) {
        return false;
    }

    _delay_until = esp_get_millis() + action()->duration;

    return true;
}

bool LedOffActionRunner::update() {
    set_led_state(false);

    return false;
}

LedBlinkActionRunner::LedBlinkActionRunner(LedAction* action) : LedActionRunner(action) {
    if (action->duration > 0) {
        _max_runtime = esp_get_millis() + action->duration;
    } else {
        _max_runtime = 0;
    }

    _next_state = true;
}

bool LedBlinkActionRunner::update() {
    const auto millis = esp_get_millis();

    if (_max_runtime && millis >= _max_runtime) {
        return false;
    }

    if (millis >= _delay_until) {
        set_led_state(_next_state);

        _delay_until = millis + (_next_state ? action()->on : action()->off);
        _next_state = !_next_state;
    }

    return true;
}
