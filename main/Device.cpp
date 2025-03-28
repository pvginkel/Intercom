#include "support.h"

#include "Device.h"

#include "nvs_flash.h"

LOG_TAG(Device);

Device::Device(MQTTConnection& mqtt_connection, Controls& controls)
    : _mqtt_connection(mqtt_connection), _controls(controls) {}

void Device::begin() {
    load_state();

    _recording_device.on_recording_changed([this](bool recording) {
        if (_state.recording != recording) {
            _state.recording = recording;
            state_changed();
        }
    });
    _playback_device.on_playing_changed([this](bool playing) {
        if (_state.playing != playing) {
            _state.playing = playing;
            state_changed();
        }
    });

    _playback_device.on_buffer_exhausted([this]() { _playback_device.stop(); });

    _recording_device.on_data_available([this](auto data) { _mqtt_connection.send_audio(data); });

    _mqtt_connection.on_enabled_changed([this](bool enabled) {
        _state.enabled = enabled;
        state_changed();
        save_state();
    });
    _mqtt_connection.on_recording_changed([this](bool recording) {
        if (recording) {
            _recording_device.start();
        } else {
            _recording_device.stop();
        }
    });
    _mqtt_connection.on_stream_data([this](auto data) {
        if (!_playback_device.is_playing()) {
            _playback_device.start();
        }

        _playback_device.add_samples(data.topic, data.buffer.buffer(), data.buffer.len());
    });

    _mqtt_connection.on_red_led_changed([this](auto action) { _controls.set_red_led(action); });
    _mqtt_connection.on_green_led_changed([this](auto action) { _controls.set_green_led(action); });

    _controls.on_red_led_active_changed([this](bool active) {
        _state.red_led = active;
        state_changed();
    });
    _controls.on_green_led_active_changed([this](bool active) {
        _state.green_led = active;
        state_changed();
    });

    _mqtt_connection.on_stream_subscribed([this](auto topic) {
        if (find(_state.subscribed_streams.begin(), _state.subscribed_streams.end(), topic) ==
            _state.subscribed_streams.end()) {
            _state.subscribed_streams.push_back(topic);
            state_changed();
        }
    });
    _mqtt_connection.on_stream_unsubscribed([this](auto topic) {
        if (auto it = find(_state.subscribed_streams.begin(), _state.subscribed_streams.end(), topic);
            it != _state.subscribed_streams.end()) {
            _state.subscribed_streams.erase(it);
            state_changed();
        }
    });

    _controls.on_press([this]() { _mqtt_connection.send_action(DeviceAction::Click); });
    _controls.on_long_press([this]() { _mqtt_connection.send_action(DeviceAction::LongClick); });

    _recording_device.begin();
    _playback_device.begin();

    state_changed();
}

void Device::state_changed() { _mqtt_connection.send_state(_state); }

void Device::load_state() {
    // Initialize defaults.

    _state.enabled = true;

    // Load previously stored values.

    nvs_handle_t handle;
    auto err = nvs_open("storage", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return;
    }
    ESP_ERROR_CHECK(err);

    int8_t enabled;
    err = nvs_get_i8(handle, "enabled", &enabled);
    if (err == ESP_OK) {
        _state.enabled = enabled;
    }

    nvs_close(handle);
}

void Device::save_state() {
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &handle));

    ESP_ERROR_CHECK(nvs_set_i8(handle, "enabled", int8_t(_state.enabled)));

    nvs_close(handle);
}
