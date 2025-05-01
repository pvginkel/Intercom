#include "support.h"

#include "Controls.h"

LOG_TAG(Controls);

constexpr ledc_channel_t LR_CHANNEL = LEDC_CHANNEL_0;
constexpr ledc_channel_t LG_CHANNEL = LEDC_CHANNEL_1;

constexpr float GAMMA = 0.5f;

static float gamma_correct(float level) { return pow(level, 1.0f / GAMMA); }

void Controls::begin() {
    _led_channel_manager.begin();

    _led_channel_manager.configure_channel(CONFIG_DEVICE_LR_PIN, LR_CHANNEL);
    _led_channel_manager.configure_channel(CONFIG_DEVICE_LG_PIN, LG_CHANNEL);

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

    if (_enabled) {
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
}

void Controls::set_red_led(LedAction* action) { set_red_runner(create_led_action_runner(action)); }

void Controls::set_red_runner(LedRunner* runner) {
    _red_led_active_changed.call(true);

    if (_red_led_runner) {
        delete _red_led_runner;
    }

    _red_led_runner = runner;
    _red_led_runner->on_led_changed(
        [this](float level) { _led_channel_manager.set_level(LR_CHANNEL, gamma_correct(level)); });
}

void Controls::set_green_led(LedAction* action) { set_green_runner(create_led_action_runner(action)); }

void Controls::set_green_runner(LedRunner* runner) {
    _green_led_active_changed.call(true);

    if (_green_led_runner) {
        delete _green_led_runner;
    }

    _green_led_runner = runner;
    _green_led_runner->on_led_changed(
        [this](float level) { _led_channel_manager.set_level(LG_CHANNEL, gamma_correct(level)); });
}

LedRunner* Controls::create_led_action_runner(LedAction* action) {
    LedRunner* runner;

    switch (action->state) {
        case LedState::On:
            runner = new LedOnRunner(action->duration);
            break;
        case LedState::Off:
            runner = new LedOffRunner();
            break;
        case LedState::Blink:
            runner = new LedBlinkRunner(action->duration, action->on, action->off);
            break;
        default:
            assert(false);
            return nullptr;
    }

    delete action;

    return runner;
}
