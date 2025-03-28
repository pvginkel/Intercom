#include "support.h"

#include "I2SRecordingDevice.h"

LOG_TAG(I2SRecordingDevice);

void I2SRecordingDevice::begin() {
    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_config, NULL, &_chan));

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_DEVICE_I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(CONFIG_DEVICE_I2S_DATA_BIT_WIDTH, I2S_SLOT_MODE_STEREO),
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

            xTaskCreate([](void *param) { ((I2SRecordingDevice *)param)->read_task(); },
                        "I2SRecordingDevice::read_task", CONFIG_ESP_MAIN_TASK_STACK_SIZE, this, 5, nullptr);
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

void I2SRecordingDevice::read_task() {
    const auto buffer_len = CONFIG_DEVICE_AUDIO_CHUNK_LEN;
    const auto buffer = (uint8_t *)malloc(buffer_len);

    ESP_ERROR_CHECK(i2s_channel_enable(_chan));

    while (_recording) {
        size_t read;
        ESP_ERROR_CHECK(i2s_channel_read(_chan, buffer, buffer_len, &read, portMAX_DELAY));

        _data_available.call({buffer, read});
    }

    ESP_LOGI(TAG, "Exiting read task");

    ESP_ERROR_CHECK(i2s_channel_disable(_chan));

    free(buffer);
    vTaskDelete(nullptr);
}
