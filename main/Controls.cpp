#include "includes.h"

#include "Controls.h"

LOG_TAG(Controls);

void Controls::begin() {
    gpio_config_t led_config = {
        .pin_bit_mask = (1ull << CONFIG_DEVICE_LR_PIN) | (1ull << CONFIG_DEVICE_LG_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_config);

    gpio_config_t btn_config = {
        .pin_bit_mask = (1ull << CONFIG_DEVICE_PB_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn_config);

    _button.setInverted();
    _button.interval(50);
    _button.attach(CONFIG_DEVICE_PB_PIN);
}

void Controls::update() {
    if (_red_led_runner && !_red_led_runner->update()) {
        delete _red_led_runner;
        _red_led_runner = nullptr;

        _red_led_active_changed.call(false);
    }
    if (_green_led_runner && !_green_led_runner->update()) {
        delete _green_led_runner;
        _green_led_runner = nullptr;

        _green_led_active_changed.call(false);
    }

    _button.update();

    const auto millis = esp_get_millis();

    if (_button_down && millis - _button_down >= CONFIG_DEVICE_LONG_PRESS_MS) {
        _button_down = 0;

        _long_press.queue(_queue);
    } else if (_button.fell()) {
        if (_button_down) {
            _button_down = 0;

            _press.queue(_queue);
        }
    } else if (_button.rose()) {
        _button_down = millis;
    }
}

void Controls::set_red_led(LedAction* action) {
    _red_led_active_changed.call(true);

    if (_red_led_runner) {
        delete _red_led_runner;
    }

    _red_led_runner = create_led_action_runner(action, (gpio_num_t)CONFIG_DEVICE_LR_PIN);
}

void Controls::set_green_led(LedAction* action) {
    _green_led_active_changed.call(true);

    if (_green_led_runner) {
        delete _green_led_runner;
    }

    _green_led_runner = create_led_action_runner(action, (gpio_num_t)CONFIG_DEVICE_LG_PIN);
}

LedActionRunner* Controls::create_led_action_runner(LedAction* action, gpio_num_t pin) {
    LedActionRunner* runner;

    switch (action->state) {
        case LedState::On:
            runner = new LedOnActionRunner(action);
            break;
        case LedState::Off:
            runner = new LedOffActionRunner(action);
            break;
        case LedState::Blink:
            runner = new LedBlinkActionRunner(action);
            break;
        default:
            assert(false);
            return nullptr;
    }

    runner->on_led_changed([pin](bool state) { gpio_set_level((gpio_num_t)pin, state); });

    return runner;
}
