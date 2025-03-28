#pragma once

#include <atomic>

#include "Callback.h"
#include "Mutex.h"
#include "RingBuffer.h"
#include "Signal.h"
#include "Span.h"
#include "driver/i2s_std.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"

class I2SRecordingDevice {
    i2s_chan_handle_t _chan;
    srmodel_list_t *_models;
    esp_afe_sr_iface_t *_afe_handle;
    esp_afe_sr_data_t *_afe_data;
    atomic<bool> _recording{};
    Callback<bool> _recording_changed;
    Mutex _lock;
    Signal _signal;
    Callback<Span<uint8_t>> _data_available;

public:
    void begin();
    void on_recording_changed(function<void(bool)> func) { _recording_changed.add(func); }
    bool is_recording() { return _recording; }
    bool start();
    bool stop();
    void on_data_available(function<void(Span<uint8_t>)> func) { _data_available.add(func); }

private:
    void read_task();
    void forward_task();
    void begin_i2s();
    void begin_afe();
};
