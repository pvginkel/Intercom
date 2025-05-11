#pragma once

#include <atomic>

#include "AudioConfiguration.h"
#include "AudioMixer.h"
#include "AutoVolume.h"
#include "Callback.h"
#include "I2SRecordingDevice.h"
#include "Mutex.h"
#include "driver/i2s_std.h"

class I2SPlaybackDevice {
    I2SRecordingDevice& _recording_device;
    i2s_chan_handle_t _chan;
    atomic<bool> _playing;
    Callback<bool> _playing_changed;
    Mutex _task_lock;
    Mutex _lock;
    AudioMixer _buffer;
    Callback<void> _buffer_exhausted;
    AutoVolume _auto_volume;
    Callback<float> _volume_changed;
    uint8_t* _write_buffer;
    size_t _write_buffer_len;
    float _volume_scale_low;
    float _volume_scale_high;
    bool _auto_volume_enabled;

public:
    I2SPlaybackDevice(I2SRecordingDevice& recording_device) : _recording_device(recording_device) {}

    void begin(const AudioConfiguration& audio_config);
    void set_volume(float volume);
    void on_playing_changed(function<void(bool)> func) { _playing_changed.add(func); }
    void on_buffer_exhausted(function<void()> func) { _buffer_exhausted.add(func); }
    void on_volume_changed(function<void(float)> func) { _volume_changed.add(func); }
    bool is_playing() { return _playing; }
    bool start();
    bool stop();
    void add_samples(sockaddr_in* source_addr, uint8_t* buffer, size_t buffer_len);

private:
    void write_task();
};
