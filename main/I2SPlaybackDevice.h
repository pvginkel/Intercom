#pragma once

#include <atomic>

#include "AudioMixer.h"
#include "Callback.h"
#include "Mutex.h"

class I2SPlaybackDevice {
    i2s_chan_handle_t _chan;
    atomic<bool> _playing;
    Callback<bool> _playing_changed;
    Mutex _lock;
    AudioMixer _buffer;
    Callback<void> _buffer_exhausted;

public:
    void begin();
    void on_playing_changed(function<void(bool)> func) { _playing_changed.add(func); }
    void on_buffer_exhausted(function<void()> func) { _buffer_exhausted.add(func); }
    bool is_playing() { return _playing; }
    bool start();
    bool stop();
    void add_samples(const string& topic, uint8_t* buffer, size_t buffer_len);

private:
    void write_task();
};
