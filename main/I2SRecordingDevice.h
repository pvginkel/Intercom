#pragma once

#include <atomic>

#include "AudioConfiguration.h"
#include "Callback.h"
#include "Mutex.h"
#include "RingBuffer.h"
#include "Signal.h"
#include "Span.h"
#include "UDPServer.h"
#include "driver/i2s_std.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"

class I2SRecordingDevice {
    UDPServer &_udp_server;
    i2s_chan_handle_t _chan;
    srmodel_list_t *_models;
    esp_afe_sr_iface_t *_afe_handle;
    esp_afe_sr_data_t *_afe_data;
    atomic<bool> _recording{};
    Callback<bool> _recording_changed;
    Mutex _task_lock;
    Mutex _lock;
    Signal _signal;
    Callback<Span<uint8_t>> _data_available;
    RingBuffer _feed_buffer;
    int64_t _feed_buffer_end_time{};
    int16_t *_work_buffer;
    size_t _work_buffer_len;
    int16_t *_reference_buffer;
    void *_read_buffer;
    size_t _read_buffer_len;
    uint8_t _microphone_gain_bits;
    bool _auto_volume_enabled;
    float _smoothing_factor;
    bool _enable_audio_processing;
#ifdef CONFIG_DEVICE_DUMP_AFE_INPUT
    sockaddr_in _dump_target;
#endif

public:
    I2SRecordingDevice(UDPServer &udp_server);

    void begin(const AudioConfiguration &audio_config);
    void on_recording_changed(function<void(bool)> func) { _recording_changed.add(func); }
    bool is_recording() { return _recording; }
    bool start();
    bool stop();
    void on_data_available(function<void(Span<uint8_t>)> func) { _data_available.add(func); }
    void reset_feed_buffer();
    void feed_reference_samples(int64_t time, uint8_t *buffer, size_t len);

private:
    void read_task();
    void forward_task();
    void begin_i2s();
    void begin_afe();
    int32_t scale_sample(int32_t sample, float &smoothed_peak);
};
