#pragma once

#include "Queue.h"

class Buttons {
    struct Button {
        Buttons* parent;
        int pin;
    };

    static void IRAM_ATTR gpio_isr_handler(void* arg);

    uint32_t _last_millis;
    QueueHandle_t _task_queue;
    Queue* _queue;
    Callback<void> _push;

public:
    Buttons(Queue* queue) : _queue(queue) {}

    void begin();

    void on_push(function<void(void)> func) { _push.add(func); }
};
