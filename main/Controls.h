#pragma once

#include <atomic>

#include "Bounce2.h"
#include "LedAction.h"
#include "LedChannelManager.h"
#include "Queue.h"

class Controls {
    Queue* _queue;
    Bounce _button;
    time_t _button_down{};
    Callback<void> _press;
    Callback<void> _long_press;
    LedRunner* _red_led_runner{};
    LedRunner* _green_led_runner{};
    Callback<bool> _red_led_active_changed;
    Callback<bool> _green_led_active_changed;
    LedChannelManager _led_channel_manager;
    std::atomic<bool> _enabled;

public:
    Controls(Queue* queue) : _queue(queue), _led_channel_manager(LEDC_TIMER_13_BIT) {}

    void begin();
    void update();

    void set_enabled(bool enabled) { _enabled = enabled; }

    void set_red_led(LedAction* action);
    void set_red_runner(LedRunner* runner);
    void set_green_led(LedAction* action);
    void set_green_runner(LedRunner* runner);

    void on_press(function<void()> func) { _press.add(func); }
    void on_long_press(function<void()> func) { _long_press.add(func); }
    void on_red_led_active_changed(function<void(bool)> func) { _red_led_active_changed.add(func); }
    void on_green_led_active_changed(function<void(bool)> func) { _green_led_active_changed.add(func); }

private:
    LedRunner* create_led_action_runner(LedAction* action);
};
