#include "support.h"

#include "I2SPlaybackDevice.h"

LOG_TAG(I2SPlaybackDevice);

void I2SPlaybackDevice::begin(const AudioConfiguration &audio_config) {
    _volume_scale_low = audio_config.volume_scale_low;
    _volume_scale_high = audio_config.volume_scale_high;
    _auto_volume_enabled = audio_config.playback_auto_volume_enabled;

    _buffer.initialize(audio_config.audio_buffer_ms);

    _auto_volume.set_target_db(audio_config.playback_target_db);

    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_config, &_chan, NULL));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_DEVICE_I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(CONFIG_DEVICE_I2S_DATA_BIT_WIDTH, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = (gpio_num_t)CONFIG_DEVICE_SPEAKER_SCK_PIN,
                .ws = (gpio_num_t)CONFIG_DEVICE_SPEAKER_WS_PIN,
                .dout = (gpio_num_t)CONFIG_DEVICE_SPEAKER_DATA_PIN,
                .din = I2S_GPIO_UNUSED,
                .invert_flags =
                    {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv = false,
                    },
            },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(_chan, &tx_std_cfg));

    _write_buffer_len = AUDIO_BUFFER_LEN(CONFIG_DEVICE_AUDIO_CHUNK_MS);
    _write_buffer = (uint8_t *)heap_caps_malloc(_write_buffer_len, MALLOC_CAP_INTERNAL);
    ESP_ERROR_ASSERT(_write_buffer);
}

void I2SPlaybackDevice::set_volume(float volume) {
    volume = clamp(volume, 0.0f, 1.0f);

    const auto scaled_volume = _volume_scale_low + (_volume_scale_high - _volume_scale_low) * volume;

    _auto_volume.set_offset_db(scaled_volume);

    _volume_changed.call(volume);
}

bool I2SPlaybackDevice::start() {
    bool result = false;

    {
        auto guard = _lock.take();

        if (_playing) {
            ESP_LOGE(TAG, "Starting playback while device is still playing");
        } else {
            ESP_LOGI(TAG, "Starting playback");

            result = true;
            _playing = true;

            _buffer.reset();

            FREERTOS_CHECK(xTaskCreatePinnedToCore(
                [](void *param) {
                    ((I2SPlaybackDevice *)param)->write_task();

                    vTaskDelete(nullptr);
                },
                "write_task", CONFIG_ESP_MAIN_TASK_STACK_SIZE, this, 5, nullptr, 0));
        }
    }

    if (result) {
        _playing_changed.call(true);
    }

    return true;
}

bool I2SPlaybackDevice::stop() {
    bool result = false;

    {
        auto guard = _lock.take();

        if (!_playing) {
            ESP_LOGE(TAG, "Stopping playback while the device isn't playing");
        } else {
            ESP_LOGI(TAG, "Stopping playback");

            result = true;
            _playing = false;
        }
    }

    if (result) {
        _playing_changed.call(false);
    }

    return result;
}

void I2SPlaybackDevice::add_samples(sockaddr_in *source_addr, uint8_t *buffer, size_t buffer_len) {
    auto guard = _lock.take();

    _buffer.append(source_addr, buffer, buffer_len);
}

void I2SPlaybackDevice::write_task() {
    auto task_guard = _task_lock.take();

    // Wait a little bit to give the buffer some time to collect data.

    vTaskDelay(pdMS_TO_TICKS(10));

    // Clear the DMA buffers.

    int32_t preloaded_samples = 0;

    memset(_write_buffer, 0, _write_buffer_len);

    while (true) {
        size_t written;
        ESP_ERROR_CHECK(i2s_channel_preload_data(_chan, _write_buffer, _write_buffer_len, &written));

        preloaded_samples += written / sizeof(int16_t);

        if (written < _write_buffer_len) {
            break;
        }
    }

    _recording_device.reset_feed_buffer();

    ESP_ERROR_CHECK(i2s_channel_enable(_chan));

    // Calculate at what time sound should be playing from the buffer. We take the
    // time that we enable the channel, plus the preloaded data above.

    auto playback_time = esp_timer_get_time() + SAMPLES_TO_US(preloaded_samples);

    while (_playing) {
        bool has_data;

        {
            auto guard = _lock.take();

            has_data = _buffer.has_data();
            if (has_data) {
                _buffer.take(_write_buffer, _write_buffer_len);
            }
        }

        if (!has_data) {
            ESP_LOGI(TAG, "Buffer exhausted");

            _buffer_exhausted.call();
            break;
        }

        if (_auto_volume_enabled) {
            _auto_volume.process_block((int16_t *)_write_buffer, _write_buffer_len / sizeof(int16_t));
        }

        _recording_device.feed_reference_samples(playback_time, _write_buffer, _write_buffer_len);
        playback_time += SAMPLES_TO_US(_write_buffer_len / sizeof(int16_t));

        ESP_ERROR_CHECK(i2s_channel_write(_chan, _write_buffer, _write_buffer_len, nullptr, portMAX_DELAY));
    }

    ESP_LOGI(TAG, "Exiting write task");

    _recording_device.reset_feed_buffer();

    ESP_ERROR_CHECK(i2s_channel_disable(_chan));
}
