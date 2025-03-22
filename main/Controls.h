#pragma once

#include "Bounce2.h"
#include "LedAction.h"
#include "Queue.h"

class Controls {
    Queue* _queue;
    Bounce _button;
    time_t _button_down{};
    Callback<void> _press;
    Callback<void> _long_press;
    LedActionRunner* _red_led_runner{};
    LedActionRunner* _green_led_runner{};
    Callback<bool> _red_led_active_changed;
    Callback<bool> _green_led_active_changed;

public:
    Controls(Queue* queue) : _queue(queue) {}

    void begin();
    void update();

    void set_red_led(LedAction* action);
    void set_green_led(LedAction* action);

    void on_press(function<void()> func) { _press.add(func); }
    void on_long_press(function<void()> func) { _long_press.add(func); }
    void on_red_led_active_changed(function<void(bool)> func) { _red_led_active_changed.add(func); }
    void on_green_led_active_changed(function<void(bool)> func) { _green_led_active_changed.add(func); }

private:
    LedActionRunner* create_led_action_runner(LedAction* action, gpio_num_t pin);
};
