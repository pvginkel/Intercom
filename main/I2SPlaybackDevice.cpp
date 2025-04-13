#include "support.h"

#include "I2SPlaybackDevice.h"

LOG_TAG(I2SPlaybackDevice);

void I2SPlaybackDevice::begin() {
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

            xTaskCreatePinnedToCore(
                [](void *param) {
                    ((I2SPlaybackDevice *)param)->write_task();

                    vTaskDelete(nullptr);
                },
                "write_task", CONFIG_ESP_MAIN_TASK_STACK_SIZE, this, 5, nullptr, 0);
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

    const auto buffer_len = CONFIG_DEVICE_AUDIO_CHUNK_LEN;
    const auto buffer = (uint8_t *)malloc(buffer_len);
    ESP_ERROR_ASSERT(buffer);

    // Wait a little bit to give the buffer some time to collect data.

    vTaskDelay(pdMS_TO_TICKS(10));

    // Clear the DMA buffers.

    int32_t preloaded_samples = 0;

    memset(buffer, 0, buffer_len);

    while (true) {
        size_t written;
        ESP_ERROR_CHECK(i2s_channel_preload_data(_chan, buffer, buffer_len, &written));

        preloaded_samples += written / sizeof(int16_t);

        if (written < buffer_len) {
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
                _buffer.take(buffer, buffer_len);
            }
        }

        if (!has_data) {
            ESP_LOGI(TAG, "Buffer exhausted");

            _buffer_exhausted.call();
            break;
        }

        _recording_device.feed_reference_samples(playback_time, buffer, buffer_len);
        playback_time += SAMPLES_TO_US(buffer_len / sizeof(int16_t));

        ESP_ERROR_CHECK(i2s_channel_write(_chan, buffer, buffer_len, nullptr, portMAX_DELAY));
    }

    ESP_LOGI(TAG, "Exiting write task");

    _recording_device.reset_feed_buffer();

    ESP_ERROR_CHECK(i2s_channel_disable(_chan));

    free(buffer);
}
