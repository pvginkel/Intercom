#include "support.h"

#include "Device.h"

#include "NVSProperty.h"

LOG_TAG(Device);

static NVSPropertyI1 nvs_enabled("enabled");
static NVSPropertyF32 nvs_volume("volume");
static NVSPropertyF32 nvs_volume_scale_low("play_vol_low");
static NVSPropertyF32 nvs_volume_scale_high("play_vol_high");
static NVSPropertyI1 nvs_enable_audio_processing("en_audio_proc");
static NVSPropertyU32 nvs_audio_buffer_ms("audio_buf_ms");
static NVSPropertyU8 nvs_microphone_gain_bits("mic_gain_bits");
static NVSPropertyI1 nvs_recording_auto_volume_enabled("rec_autovol_en");
static NVSPropertyF32 nvs_recording_smoothing_factor("rec_smooth_fac");
static NVSPropertyI1 nvs_playback_auto_volume_enabled("play_autovol_en");
static NVSPropertyF32 nvs_playback_target_db("play_target_db");

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

void Device::load_state() {
    nvs_handle_t handle;
    auto err = nvs_open("storage", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return;
    }
    ESP_ERROR_CHECK(err);

    _state.enabled = nvs_enabled.get(handle, true);
    _state.volume = nvs_volume.get(handle, 0.6);

    _state.audio_config.volume_scale_low = nvs_volume_scale_low.get(handle, -30);
    _state.audio_config.volume_scale_high = nvs_volume_scale_high.get(handle, -8);
    _state.audio_config.enable_audio_processing = nvs_enable_audio_processing.get(handle, true);
    _state.audio_config.audio_buffer_ms = nvs_audio_buffer_ms.get(handle, 200);
    _state.audio_config.microphone_gain_bits = nvs_microphone_gain_bits.get(handle, 3);
    _state.audio_config.recording_auto_volume_enabled = nvs_recording_auto_volume_enabled.get(handle, false);
    _state.audio_config.recording_smoothing_factor = nvs_recording_smoothing_factor.get(handle, 0.1);
    _state.audio_config.playback_auto_volume_enabled = nvs_playback_auto_volume_enabled.get(handle, true);
    _state.audio_config.playback_target_db = nvs_playback_target_db.get(handle, -14);

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

    nvs_enabled.set(handle, _state.enabled);
    nvs_volume.set(handle, _state.volume);

    nvs_volume_scale_low.set(handle, _state.audio_config.volume_scale_low);
    nvs_volume_scale_high.set(handle, _state.audio_config.volume_scale_high);
    nvs_enable_audio_processing.set(handle, _state.audio_config.enable_audio_processing);
    nvs_audio_buffer_ms.set(handle, _state.audio_config.audio_buffer_ms);
    nvs_microphone_gain_bits.set(handle, _state.audio_config.microphone_gain_bits);
    nvs_recording_auto_volume_enabled.set(handle, _state.audio_config.recording_auto_volume_enabled);
    nvs_recording_smoothing_factor.set(handle, _state.audio_config.recording_smoothing_factor);
    nvs_playback_auto_volume_enabled.set(handle, _state.audio_config.playback_auto_volume_enabled);
    nvs_playback_target_db.set(handle, _state.audio_config.playback_target_db);

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
