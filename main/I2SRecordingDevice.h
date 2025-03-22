#pragma once

#include <atomic>

#include "Callback.h"
#include "Mutex.h"
#include "RingBuffer.h"
#include "Signal.h"
#include "Span.h"

class I2SRecordingDevice {
    i2s_chan_handle_t _chan;
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
};
