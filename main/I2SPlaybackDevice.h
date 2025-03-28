#pragma once

#include <atomic>

#include "AudioMixer.h"
#include "Callback.h"
#include "I2SRecordingDevice.h"
#include "Mutex.h"
#include "driver/i2s_std.h"

class I2SPlaybackDevice {
    I2SRecordingDevice* _recording_device;
    i2s_chan_handle_t _chan;
    atomic<bool> _playing;
    Callback<bool> _playing_changed;
    Mutex _lock;
    AudioMixer _buffer;
    Callback<void> _buffer_exhausted;

public:
    I2SPlaybackDevice(I2SRecordingDevice* recording_device) : _recording_device(recording_device) {}

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
