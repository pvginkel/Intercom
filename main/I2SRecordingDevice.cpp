#include "support.h"

#include "I2SRecordingDevice.h"

LOG_TAG(I2SRecordingDevice);

I2SRecordingDevice::I2SRecordingDevice(UDPServer &udp_server) : _udp_server(udp_server) {
#ifdef CONFIG_DEVICE_DUMP_AFE_INPUT
    parse_endpoint(&_dump_target, CONFIG_DEVICE_DUMP_AFE_INPUT_TARGET);
#endif
}

void I2SRecordingDevice::begin(const AudioConfiguration &audio_config) {
    _enable_audio_processing = audio_config.enable_audio_processing;
    _microphone_gain_bits = audio_config.microphone_gain_bits;
    _auto_volume_enabled = audio_config.recording_auto_volume_enabled;
    _smoothing_factor = audio_config.recording_smoothing_factor;

    _feed_buffer.initialize(AUDIO_BUFFER_LEN(audio_config.audio_buffer_ms * 2));

    begin_i2s();
    begin_afe();

    const auto feed_chunksize = _afe_handle->get_feed_chunksize(_afe_data);
    const auto feed_nch = _afe_handle->get_feed_channel_num(_afe_data);
    assert(feed_nch == 2);
    _work_buffer_len = feed_chunksize * feed_nch * sizeof(int16_t);
    _work_buffer = (int16_t *)heap_caps_malloc(_work_buffer_len, MALLOC_CAP_INTERNAL);
    ESP_ERROR_ASSERT(_work_buffer);
    _reference_buffer = (int16_t *)heap_caps_malloc(_work_buffer_len, MALLOC_CAP_INTERNAL);
    ESP_ERROR_ASSERT(_reference_buffer);

    // We keep the sample buffer equal to the feed buffer to keep latency down.
    _read_buffer_len = feed_chunksize * 1 /* mono */ * sizeof(int32_t);
    _read_buffer = heap_caps_malloc(_read_buffer_len, MALLOC_CAP_INTERNAL);
    ESP_ERROR_ASSERT(_read_buffer);

    FREERTOS_CHECK(xTaskCreatePinnedToCore(
        [](void *param) {
            ((I2SRecordingDevice *)param)->forward_task();

            vTaskDelete(nullptr);
        },
        "forward_task", CONFIG_ESP_MAIN_TASK_STACK_SIZE, this, 5, nullptr, 0));
}

void I2SRecordingDevice::begin_i2s() {
    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_config, NULL, &_chan));

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_DEVICE_I2S_SAMPLE_RATE),
        .slot_cfg =
            {
                .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
                .slot_mode = I2S_SLOT_MODE_MONO,
                .slot_mask = I2S_STD_SLOT_LEFT,
                .ws_width = 32,
                .ws_pol = false,
                .bit_shift = false,
                .left_align = true,
                .big_endian = false,
                .bit_order_lsb = false,
            },
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = (gpio_num_t)CONFIG_DEVICE_MICROPHONE_SCK_PIN,
                .ws = (gpio_num_t)CONFIG_DEVICE_MICROPHONE_WS_PIN,
                .dout = I2S_GPIO_UNUSED,
                .din = (gpio_num_t)CONFIG_DEVICE_MICROPHONE_DATA_PIN,
                .invert_flags =
                    {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv = false,
                    },
            },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(_chan, &rx_std_cfg));
}

void I2SRecordingDevice::begin_afe() {
    // Quick and dirty way to properly initialize an empty models data structure.
    // The first four bytes of the model partition is the number of models.
    int32_t model_count = 0;
    _models = srmodel_load(&model_count);

    // _models = esp_srmodel_init("model");

    auto afe_config = afe_config_init("MR", _models, AFE_TYPE_VC, AFE_MODE_HIGH_PERF);

    /********** AEC(Acoustic Echo Cancellation) **********/
    // Whether to init aec
    assert(afe_config->aec_init == true);
    // The mode of aec, AEC_MODE_SR_LOW_COST or AEC_MODE_SR_HIGH_PERF
    // afe_config->aec_mode;
    afe_config->aec_mode = AEC_MODE_VOIP_HIGH_PERF;
    // The filter length of aec
    // afe_config->aec_filter_length;
    assert(afe_config->aec_filter_length == 4);
    // afe_config->aec_filter_length = 8;

    /********** SE(Speech Enhancement, microphone array processing) **********/
    // Whether to init se
    assert(afe_config->se_init == false);

    /********** NS(Noise Suppression) **********/
    // Whether to init ns
    assert(afe_config->ns_init == true);
    // Model name of ns
    // afe_config->ns_model_name;
    // Model mode of ns
    // afe_config->afe_ns_mode;

    /********** VAD(Voice Activity Detection) **********/
    // Whether to init vad
    afe_config->vad_init = false;
    // The value can be: VAD_MODE_0, VAD_MODE_1, VAD_MODE_2, VAD_MODE_3, VAD_MODE_4
    // afe_config->vad_mode;
    // The model name of vad, If it is null, WebRTC VAD will be used.
    // afe_config->vad_model_name;
    // The minimum duration of speech in ms. It should be bigger than 32 ms, default: 128 ms
    // afe_config->vad_min_speech_ms;
    // The minimum duration of noise or silence in ms. It should be bigger than 64 ms, default:
    // 1000 ms
    // afe_config->vad_min_noise_ms;
    // The delay of the first speech frame in ms, default: 128 ms
    // If you find vad cache can not cover all speech, please increase this value.
    // afe_config->vad_delay_ms;
    // If true, the playback will be muted for vad detection. default: false
    // afe_config->vad_mute_playback;
    // If true, the vad will be used to choose the channel id. default: false
    // afe_config->vad_enable_channel_trigger;

    /********** WakeNet(Wake Word Engine) **********/
    assert(afe_config->wakenet_init == false);
    // The model name of wakenet 1
    // afe_config->wakenet_model_name;
    // The model name of wakenet 2 if has wakenet 2
    // afe_config->wakenet_model_name_2;
    // The mode of wakenet
    // afe_config->wakenet_mode;

    /********** AGC(Automatic Gain Control) **********/
    // Whether to init agc
    afe_config->agc_init = true;
    // The AGC mode for ASR. and the gain generated by AGC acts on the audio after far linear gain.
    assert(afe_config->agc_mode == AFE_AGC_MODE_WEBRTC);
    // Compression gain in dB (default 9)
    assert(afe_config->agc_compression_gain_db == 9);
    // Target level in -dBfs of envelope (default -3)
    assert(afe_config->agc_target_level_dbfs == 3);

    /********** General AFE(Audio Front End) parameter **********/
    // Config the channel num of original data which is fed to the afe feed function.
    // afe_config->pcm_config;
    // The mode of afe， AFE_MODE_LOW_COST or AFE_MODE_HIGH_PERF
    // afe_config->afe_mode;
    // The mode of afe， AFE_MODE_LOW_COST or AFE_MODE_HIGH_PERF
    // afe_config->afe_type;
    // The preferred core of afe se task, which is created in afe_create function.
    afe_config->afe_perferred_core = 1;
    // afe_config->afe_perferred_core;
    // The preferred priority of afe se task, which is created in afe_create function.
    // afe_config->afe_perferred_priority;
    // The ring buffer size: the number of frame data in ring buffer.
    // afe_config->afe_ringbuf_size;
    // The memory alloc mode for afe. From Internal RAM or PSRAM
    // afe_config->memory_alloc_mode;
    // The linear gain for afe output the value should be in [0.1, 10.0]. This value acts directly on the output
    // amplitude: out_linear_gain * amplitude.
    // afe_config->afe_linear_gain;
    // afe_config->debug_init;
    // If true, the channel after first wake-up is fixed to raw data of microphone
    // otherwise, select channel number by wakenet
    // afe_config->fixed_first_channel;

    _afe_handle = esp_afe_handle_from_config(afe_config);
    ESP_ERROR_ASSERT(_afe_handle);
    _afe_data = _afe_handle->create_from_config(afe_config);
    ESP_ERROR_ASSERT(_afe_data);

    afe_config_free(afe_config);
}

int32_t I2SRecordingDevice::scale_sample(int32_t sample, float &smoothed_peak) {
    // This logic implements a smoothing algorithm to dynamically
    // scale the raw samples to 16 bits.

    const auto abs_sample = abs((float)(sample));

    // Update the smoothed peak:
    // - If the current sample exceeds the current smoothedPeak, update immediately.
    // - Otherwise, decay the smoothed peak slowly.

    if (abs_sample > smoothed_peak) {
        smoothed_peak = abs_sample;
    } else {
        smoothed_peak = smoothed_peak * (1.0f - _smoothing_factor) + abs_sample * _smoothing_factor;
    }

    // Prevent division by zero or very small numbers.
    if (smoothed_peak < 1.0f) {
        smoothed_peak = 1.0f;
    }

    // Calculate the gain factor to map the smoothed peak to the 16-bit maximum.
    const auto gain = min(1.0f, float(INT16_MAX) / smoothed_peak);

    return int32_t(sample * gain);
}

bool I2SRecordingDevice::start() {
    bool result = false;

    {
        auto guard = _lock.take();

        if (_recording) {
            ESP_LOGE(TAG, "Starting recorder while device is still recording");
        } else {
            ESP_LOGI(TAG, "Starting recorder");

            result = true;
            _recording = true;

            // AFE requires a significantly larger than normal stack.
            const int AFE_TASK_SIZE = 8192;

            FREERTOS_CHECK(xTaskCreatePinnedToCore(
                [](void *param) {
                    ((I2SRecordingDevice *)param)->read_task();

                    vTaskDelete(nullptr);
                },
                "read_task", AFE_TASK_SIZE, this, 5, nullptr, 1));
        }
    }

    if (result) {
        _recording_changed.call(true);
    }

    return true;
}

bool I2SRecordingDevice::stop() {
    bool result = false;

    {
        auto guard = _lock.take();

        if (!_recording) {
            ESP_LOGE(TAG, "Stopping recorder while the device isn't recording");
        } else {
            ESP_LOGI(TAG, "Stopping recorder");

            result = true;
            _recording = false;
        }
    }

    if (result) {
        _recording_changed.call(false);
    }

    return result;
}

void I2SRecordingDevice::reset_feed_buffer() {
    auto guard = _lock.take();

    _feed_buffer_end_time = 0;
    _feed_buffer.reset();
}

void I2SRecordingDevice::feed_reference_samples(int64_t time, uint8_t *buffer, size_t len) {
    auto guard = _lock.take();

    _feed_buffer.write(buffer, len);
    _feed_buffer_end_time = time + SAMPLES_TO_US(len / sizeof(int16_t));
}

void I2SRecordingDevice::read_task() {
    auto task_guard = _task_lock.take();

    size_t work_buffer_offset = 0;
    auto smoothed_peak = 1.0f;

    ESP_ERROR_CHECK(i2s_channel_enable(_chan));

    // Capture the time we start recording. We assume that we're taking samples from
    // the moment we've enabled the channel.
    auto recording_time = esp_timer_get_time();

    auto feed_buffer_synced = false;

    while (_recording) {
        size_t read;
        ESP_ERROR_CHECK(i2s_channel_read(_chan, _read_buffer, _read_buffer_len, &read, portMAX_DELAY));

        const auto samples = read / sizeof(int32_t);

        size_t reference_read = 0;

        {
            auto guard = _lock.take();

            if (!feed_buffer_synced) {
                // Sync the feed buffer with the recording offset. If the current recording
                // time lies within the data buffered in the feed buffer, sync up the head
                // of the feed buffer with the recording time and take samples from it.

                auto feed_buffer_start_time =
                    _feed_buffer_end_time - SAMPLES_TO_US(_feed_buffer.available() / sizeof(int16_t));

                if (recording_time >= feed_buffer_start_time && recording_time <= _feed_buffer_end_time) {
                    // Sync up the head of the feed buffer with the recording time.
                    const auto skip = US_TO_SAMPLES(recording_time - feed_buffer_start_time) * sizeof(int16_t);

                    _feed_buffer.skip(skip);

                    feed_buffer_synced = true;
                }
            }

            if (feed_buffer_synced) {
                reference_read = _feed_buffer.read(_reference_buffer, samples * sizeof(int16_t));
                if (reference_read < samples * sizeof(int16_t)) {
                    feed_buffer_synced = false;
                }
            }
        }

        recording_time += SAMPLES_TO_US(samples);

        const auto reference_samples = reference_read / sizeof(int16_t);

        auto source = (int32_t *)_read_buffer;

        for (int i = 0; i < samples; i++) {
            // The INMP441 writes bits 1 through 24. To align this with a 32 bit value,
            // the shift would have to be (source << 1) >> 8. We however need to compress
            // the value into 16 bits. This is done with a smoothing algorithm.
            // The value of _microphone_gain_bits determines how much
            // bits we actually give the method. The full 24 bit range is too much (it
            // picks up far to low volume audio), so we clip the bottom here already.
            //
            // The result will be 24 bits compressed into (16 + _microphone_gain_bits)
            // bits.

            const auto raw_sample = (source[i] << 1) >> (16 - _microphone_gain_bits);
            const auto scaled_sample = _auto_volume_enabled ? scale_sample(raw_sample, smoothed_peak) : raw_sample;
            const auto sample = (int16_t)clamp<int32_t>(scaled_sample, INT16_MIN, INT16_MAX);

            const auto reference_sample = i < reference_samples ? _reference_buffer[i] : 0;

            _work_buffer[work_buffer_offset++] = sample;  // Left mic.

            if (_enable_audio_processing) {
                _work_buffer[work_buffer_offset++] = reference_sample;  // Playback/reference channel.
            }

            if (work_buffer_offset * sizeof(int16_t) >= _work_buffer_len) {
                if (_enable_audio_processing) {
                    _afe_handle->feed(_afe_data, _work_buffer);
                } else {
                    _data_available.call({(uint8_t *)_work_buffer, _work_buffer_len});
                }

                work_buffer_offset = 0;

#ifdef CONFIG_DEVICE_DUMP_AFE_INPUT
                _udp_server.send((sockaddr *)&_dump_target, sizeof(_dump_target), feed_buffer, feed_buffer_len);
#endif
            }
        }
    }

    ESP_LOGI(TAG, "Exiting read task");

    ESP_ERROR_CHECK(i2s_channel_disable(_chan));
}

void I2SRecordingDevice::forward_task() {
    while (true) {
        auto res = _afe_handle->fetch_with_delay(_afe_data, portMAX_DELAY);
        ESP_ERROR_ASSERT(res);
        ESP_ERROR_CHECK(res->ret_value);

        _data_available.call({(uint8_t *)res->data, (size_t)res->data_size});
    }
}
