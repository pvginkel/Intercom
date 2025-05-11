#include "support.h"

#include "Device.h"

LOG_TAG(Device);

Device::Device(MQTTConnection& mqtt_connection, UDPServer& udp_server, Controls& controls)
    : _mqtt_connection(mqtt_connection),
      _udp_server(udp_server),
      _controls(controls),
      _recording_device(_udp_server),
      _playback_device(_recording_device) {
    _send_buffer = (uint8_t*)malloc(UDPServer::PAYLOAD_LEN);
}

void Device::begin() {
    load_state();

    _playback_device.on_buffer_exhausted([this]() { _playback_device.stop(); });

    _recording_device.on_data_available([this](auto data) { send_audio(data); });

    _recording_device.begin(_state.audio_config);

    _recording_device.on_recording_changed([this](bool recording) {
        if (_state.recording != recording) {
            _state.recording = recording;

            state_changed();
        }
    });

    _playback_device.begin(_state.audio_config);
    _playback_device.set_volume(_state.volume);

    _playback_device.on_playing_changed([this](bool playing) {
        if (_state.playing != playing) {
            _state.playing = playing;

            state_changed();
        }
    });
    _playback_device.on_volume_changed([this](float volume) {
        if (_state.volume != volume) {
            _state.volume = volume;

            state_changed();
            save_state();
        }
    });

    _mqtt_connection.on_enabled_changed([this](bool enabled) {
        _state.enabled = enabled;

        state_changed();
        save_state();
    });
    _mqtt_connection.on_recording_changed([this](bool recording) {
        if (recording) {
            // Reset the packet index to prevent wrap around.
            _next_packet_index = 0;

            _recording_device.start();
        } else {
            _recording_device.stop();
        }
    });
    _mqtt_connection.on_volume_changed([this](float volume) { _playback_device.set_volume(volume); });

    _udp_server.on_received([this](auto packet) {
        if (!_playback_device.is_playing()) {
            _playback_device.start();
        }

        _playback_device.add_samples(packet.source_addr, (uint8_t*)packet.buffer, packet.buffer_len);
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

    _mqtt_connection.on_remote_endpoint_added([this](auto endpoint) {
        auto it = find_if(_remote_endpoints.begin(), _remote_endpoints.end(),
                          [&endpoint](const Endpoint& ep) { return ep.endpoint == endpoint; });

        if (it == _remote_endpoints.end()) {
            Endpoint ep = {
                .endpoint = endpoint,
            };

            ESP_ERROR_CHECK(parse_endpoint(&ep.addr, endpoint.c_str()));

            _remote_endpoints.push_back(ep);
        }
    });
    _mqtt_connection.on_remote_endpoint_removed([this](auto endpoint) {
        auto it = find_if(_remote_endpoints.begin(), _remote_endpoints.end(),
                          [&endpoint](const Endpoint& ep) { return ep.endpoint == endpoint; });

        if (it != _remote_endpoints.end()) {
            _remote_endpoints.erase(it);
        }
    });

    _mqtt_connection.on_identify_requested([this]() {
        _controls.set_red_runner(new LedFadeRunner(0, 5000, 300));
        _controls.set_green_runner(new LedFadeRunner(1, 5000, 300));
    });

    _mqtt_connection.on_restart_requested([]() { esp_restart(); });

    _controls.on_press([this]() { _mqtt_connection.send_action(DeviceAction::Click); });
    _controls.on_long_press([this]() { _mqtt_connection.send_action(DeviceAction::LongClick); });

    _mqtt_connection.on_connected_changed([this](auto state) {
        if (state.connected) {
            state_changed();
        }
    });

    _mqtt_connection.on_audio_configuration_changed([this](auto config) {
        _state.audio_config = config;

        save_state();

        ESP_LOGI(TAG, "Audio configuration changed; restarting device");

        esp_restart();
    });
}

void Device::state_changed() {
    if (_mqtt_connection.is_connected()) {
        _mqtt_connection.send_state(_state);
    }
}

#define NVS_GET(typename, name, entry, default)                    \
    {                                                              \
        decltype(_state.entry) value;                              \
        const auto err = nvs_get_##typename(handle, name, &value); \
        if (err == ESP_OK) {                                       \
            _state.entry = value;                                  \
        } else {                                                   \
            _state.entry = default;                                \
        }                                                          \
    }

#define NVS_GET_FLOAT(name, entry, default)                 \
    {                                                       \
        uint32_t value;                                     \
        const auto err = nvs_get_u32(handle, name, &value); \
        if (err == ESP_OK) {                                \
            *(uint32_t*)&_state.entry = value;              \
        } else {                                            \
            _state.entry = default;                         \
        }                                                   \
    }

void Device::load_state() {
    nvs_handle_t handle;
    auto err = nvs_open("storage", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return;
    }
    ESP_ERROR_CHECK(err);

    NVS_GET(i1, "enabled", enabled, true);
    NVS_GET(f32, "volume", volume, -16);

    NVS_GET(f32, "play_vol_low", audio_config.volume_scale_low, -20);
    NVS_GET(f32, "play_vol_high", audio_config.volume_scale_high, -10);
    NVS_GET(i1, "en_audio_proc", audio_config.enable_audio_processing, true);
    NVS_GET(u32, "audio_buf_ms", audio_config.audio_buffer_ms, 400)
    NVS_GET(u8, "mic_gain_bits", audio_config.microphone_gain_bits, 4);
    NVS_GET(i1, "rec_autovol_en", audio_config.recording_auto_volume_enabled, true);
    NVS_GET(f32, "rec_smooth_fac", audio_config.recording_smoothing_factor, 0.1f);
    NVS_GET(i1, "play_autovol_en", audio_config.playback_auto_volume_enabled, true);
    NVS_GET(f32, "play_target_db", audio_config.playback_target_db, -18);

    nvs_close(handle);

    ESP_LOGI(TAG, "Loaded audio configuration:");
    ESP_LOGI(TAG, "  Volume scale low: %f", _state.audio_config.volume_scale_low);
    ESP_LOGI(TAG, "  Volume scale high: %f", _state.audio_config.volume_scale_high);
    ESP_LOGI(TAG, "  Enable audio processing: %s", _state.audio_config.enable_audio_processing ? "true" : "false");
    ESP_LOGI(TAG, "  Audio buffer (ms): %" PRIu32, _state.audio_config.audio_buffer_ms);
    ESP_LOGI(TAG, "  Microphone gain (bits): %d", _state.audio_config.microphone_gain_bits);
    ESP_LOGI(TAG, "  Recording auto volume enabled: %s",
             _state.audio_config.recording_auto_volume_enabled ? "true" : "false");
    ESP_LOGI(TAG, "  Recording smoothing factor: %f", _state.audio_config.recording_smoothing_factor);
    ESP_LOGI(TAG, "  Playback auto volume enabled: %s",
             _state.audio_config.playback_auto_volume_enabled ? "true" : "false");
    ESP_LOGI(TAG, "  Playback target Db: %f", _state.audio_config.playback_target_db);
}

void Device::save_state() {
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &handle));

    ESP_ERROR_CHECK(nvs_set_i1(handle, "enabled", _state.enabled));
    ESP_ERROR_CHECK(nvs_set_f32(handle, "volume", _state.volume));

    ESP_ERROR_CHECK(nvs_set_f32(handle, "play_vol_low", _state.audio_config.volume_scale_low));
    ESP_ERROR_CHECK(nvs_set_f32(handle, "play_vol_high", _state.audio_config.volume_scale_high));
    ESP_ERROR_CHECK(nvs_set_i1(handle, "en_audio_proc", _state.audio_config.enable_audio_processing));
    ESP_ERROR_CHECK(nvs_set_u32(handle, "audio_buf_ms", _state.audio_config.audio_buffer_ms));
    ESP_ERROR_CHECK(nvs_set_u8(handle, "mic_gain_bits", _state.audio_config.microphone_gain_bits));
    ESP_ERROR_CHECK(nvs_set_i1(handle, "rec_autovol_en", _state.audio_config.recording_auto_volume_enabled));
    ESP_ERROR_CHECK(nvs_set_f32(handle, "rec_smooth_fac", _state.audio_config.recording_smoothing_factor));
    ESP_ERROR_CHECK(nvs_set_i1(handle, "play_autovol_en", _state.audio_config.playback_auto_volume_enabled));
    ESP_ERROR_CHECK(nvs_set_f32(handle, "play_target_db", _state.audio_config.playback_target_db));

    nvs_close(handle);
}

void Device::send_audio(Span<uint8_t> data) {
    const size_t chunk_len = UDPServer::PAYLOAD_LEN - 4;

    for (size_t offset = 0; offset < data.len(); offset += chunk_len) {
        auto packet_index = _next_packet_index++;
        *(uint32_t*)_send_buffer = htonl((uint32_t)packet_index);

        auto this_chunk_len = min(chunk_len, data.len() - offset);

        memcpy(_send_buffer + 4, data.buffer() + offset, this_chunk_len);

        for (const auto& endpoint : _remote_endpoints) {
            _udp_server.send((sockaddr*)&endpoint.addr, sizeof(endpoint.addr), _send_buffer, this_chunk_len + 4);
        }
    }
}
