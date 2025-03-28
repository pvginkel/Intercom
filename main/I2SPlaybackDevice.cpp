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

            xTaskCreate([](void *param) { ((I2SPlaybackDevice *)param)->write_task(); },
                        "I2SPlaybackDevice::write_task", CONFIG_ESP_MAIN_TASK_STACK_SIZE, this, 5, nullptr);
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

void I2SPlaybackDevice::add_samples(const string &topic, uint8_t *buffer, size_t buffer_len) {
    auto guard = _lock.take();

    _buffer.append(topic, buffer, buffer_len);
}

void I2SPlaybackDevice::write_task() {
    const auto buffer_len_ms = CONFIG_DEVICE_AUDIO_CHUNK_MS;
    const auto buffer_len = CONFIG_DEVICE_AUDIO_CHUNK_LEN;
    const auto buffer = (uint8_t *)malloc(buffer_len);
    ESP_ERROR_ASSERT(buffer);
    auto start_ms = esp_get_millis();
    auto played_ms = 0ull;

    memset(buffer, 0, buffer_len);

    // Clear the DMA buffers.

    while (true) {
        size_t written;
        ESP_ERROR_CHECK(i2s_channel_preload_data(_chan, buffer, buffer_len, &written));
        if (written < buffer_len) {
            break;
        }
    }

    ESP_ERROR_CHECK(i2s_channel_enable(_chan));

    while (_playing) {
        // played_ms maintains the number of ms played. We play audio in
        // chunks of buffer_len_ms time. We want the current time to be
        // inside the next chunk of "buffer" time. This causes this loop
        // to be a little bit (buffer_len_ms) behind the incoming audio
        // stream. If we get too many hiccups, increase the multiplier
        // on buffer_len_ms in calculating the delay.

        auto elapsed_ms = esp_get_millis() - start_ms;
        auto delay_ms = (played_ms + buffer_len_ms) - elapsed_ms;
        if (delay_ms) {
            auto rounded_up_delay_ms = ((delay_ms + buffer_len_ms - 1) / buffer_len_ms) * buffer_len_ms;
            ESP_ERROR_ASSERT(rounded_up_delay_ms > 0 && rounded_up_delay_ms < 100);
            // ESP_LOGI(TAG, "Waiting %d", (int)rounded_up_delay_ms);
            vTaskDelay(pdMS_TO_TICKS(rounded_up_delay_ms));
        }

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

        _recording_device->feed_reference_samples(buffer, buffer_len);

        ESP_ERROR_CHECK(i2s_channel_write(_chan, buffer, buffer_len, nullptr, portMAX_DELAY));
        played_ms += buffer_len_ms;
    }

    ESP_LOGI(TAG, "Exiting write task");

    ESP_ERROR_CHECK(i2s_channel_disable(_chan));

    free(buffer);
    vTaskDelete(nullptr);
}
