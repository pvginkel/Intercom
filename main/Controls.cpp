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

void Controls::set_red_led(bool on) { gpio_set_level((gpio_num_t)CONFIG_DEVICE_LR_PIN, on); }

void Controls::set_green_led(bool on) { gpio_set_level((gpio_num_t)CONFIG_DEVICE_LG_PIN, on); }
