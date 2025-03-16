#pragma once

#include "Bounce2.h"
#include "Queue.h"

class Controls {
    Queue* _queue;
    Bounce _button;
    time_t _button_down{};
    Callback<void> _press;
    Callback<void> _long_press;

public:
    Controls(Queue* queue) : _queue(queue) {}

    void begin();
    void update();

    void set_red_led(bool on);
    void set_green_led(bool on);

    void on_press(function<void(void)> func) { _press.add(func); }
    void on_long_press(function<void(void)> func) { _long_press.add(func); }
};
