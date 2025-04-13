#include "support.h"

#include "Device.h"

#include "nvs_flash.h"

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

    _recording_device.on_data_available([this](auto data) { send_audio(data); });

    _recording_device.begin();
    _playback_device.begin();

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

    _controls.on_press([this]() { _mqtt_connection.send_action(DeviceAction::Click); });
    _controls.on_long_press([this]() { _mqtt_connection.send_action(DeviceAction::LongClick); });

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
