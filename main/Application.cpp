#include "support.h"

#include "Application.h"

#include "Messages.h"
#include "driver/i2c.h"

LOG_TAG(Application);

Application::Application()
    : _udp_server(11106), _controls(&get_queue()), _device(get_mqtt_connection(), _udp_server, _controls) {}

MQTTDeviceConfiguration Application::get_device_configuration() {
    auto config = ApplicationBase::get_device_configuration();
    config.model_id = strformat("%s v%d", config.model.c_str(), HARDWARE_VERSION);
    return config;
}

void Application::do_begin() {
    _controls.begin();

    _controls.set_red_runner(new LedFadeRunner(0, 0, 500));

    get_mqtt_connection().on_connected_changed([this](auto state) {
        if (state.connected) {
            state_changed();

            register_mqtt_callbacks();
        }
    });

    get_mqtt_connection().on_create_configuration([this](cJSON* json) {
        auto audio_formats = cJSON_AddObjectToObject(json, "audio_formats");

        auto audio_formats_in = cJSON_AddObjectToObject(audio_formats, "in");
        cJSON_AddStringToObject(audio_formats_in, "channel_layout", "mono");
        cJSON_AddNumberToObject(audio_formats_in, "sample_rate", CONFIG_DEVICE_I2S_SAMPLE_RATE);
        cJSON_AddNumberToObject(audio_formats_in, "bit_rate", CONFIG_DEVICE_I2S_BITS_PER_SAMPLE);

        auto audio_formats_out = cJSON_AddObjectToObject(audio_formats, "out");
        cJSON_AddStringToObject(audio_formats_out, "channel_layout", "mono");
        cJSON_AddNumberToObject(audio_formats_out, "sample_rate", CONFIG_DEVICE_I2S_SAMPLE_RATE);
        cJSON_AddNumberToObject(audio_formats_out, "bit_rate", CONFIG_DEVICE_I2S_BITS_PER_SAMPLE);

        cJSON_AddStringToObject(
            json, "endpoint",
            strformat("%s:%d", get_network_connection().get_ip_address(), _udp_server.get_port()).c_str());
    });
}

void Application::register_mqtt_callbacks() {
    get_mqtt_connection().publish_button_discovery(
        {
            .name = "Identify",
            .object_id = "identify",
            .entity_category = "config",
            .device_class = "identify",
        },
        [this]() {
            ESP_LOGI(TAG, "Requested identification");

            _device.identify();
        });

    get_mqtt_connection().publish_button_discovery(
        {
            .name = "Restart",
            .object_id = "restart",
            .entity_category = "config",
            .device_class = "restart",
        },
        []() {
            ESP_LOGI(TAG, "Requested restart");

            esp_restart();
        });

    get_mqtt_connection().publish_number_discovery(
        MQTTDiscovery{
            .name = "Volume",
            .object_id = "volume",
            .icon = "mdi:knob",
            .entity_category = "config",
            .device_class = "sound_pressure",
        },
        MQTTNumberDiscovery{
            .unit_of_measurement = "Db",
            .value_template = "{{ value_json.volume }}",
            .min = 0,
            .max = 1,
            .step = 0.1,
        },
        [this](const std::string& value) {
            ESP_LOGI(TAG, "Setting volume to %s", value.c_str());

            cJSON_Data root = {cJSON_Parse(value.c_str())};

            if (cJSON_IsNumber(*root)) {
                _device.set_volume((*root)->valuedouble);
            } else {
                ESP_LOGE(TAG, "Failed to parse value '%s' as float", value.c_str());
            }
        });

    get_mqtt_connection().publish_binary_sensor_discovery(
        MQTTDiscovery{
            .name = "Playing",
            .object_id = "playing",
            .icon = "mdi:play",
            .entity_category = "diagnostic",
        },
        MQTTBinarySensorDiscovery{
            .value_template = "{{ value_json.playing }}",
        });

    get_mqtt_connection().publish_binary_sensor_discovery(
        MQTTDiscovery{
            .name = "Recording",
            .object_id = "recording",
            .icon = "mdi:record",
            .entity_category = "diagnostic",
        },
        MQTTBinarySensorDiscovery{
            .value_template = "{{ value_json.recording }}",
        });

    get_mqtt_connection().publish_switch_discovery(
        MQTTDiscovery{
            .name = "Enabled",
            .object_id = "enabled",
            .icon = "mdi:toggle-switch",
            .entity_category = "config",
        },
        MQTTSwitchDiscovery{
            .value_template = "{{ value_json.enabled }}",
        },
        [this](bool enabled) {
            ESP_LOGI(TAG, "Setting enabled to %s", enabled ? "true" : "false");

            _device.set_enabled(enabled);
        });

    get_mqtt_connection().register_callback("recording", [this](const string& value) {
        ESP_LOGI(TAG, "Setting recording to %s", value.c_str());

        _device.set_recording(iequals(value, "true"));
    });

    get_mqtt_connection().register_callback("red_led", [this](const string& value) {
        ESP_LOGI(TAG, "Setting red led to %s", value.c_str());

        if (auto action = parse_led_action(value)) {
            _device.set_red_led(action);
        }
    });

    get_mqtt_connection().register_callback("green_led", [this](const string& value) {
        ESP_LOGI(TAG, "Setting green led to %s", value.c_str());

        if (auto action = parse_led_action(value)) {
            _device.set_green_led(action);
        }
    });

    get_mqtt_connection().register_callback("add_endpoint", [this](const string& value) {
        ESP_LOGI(TAG, "Adding audio recipient endpoint %s", value.c_str());

        _device.add_endpoint(value);
    });

    get_mqtt_connection().register_callback("remove_endpoint", [this](const string& value) {
        ESP_LOGI(TAG, "Removing audio recipient endpoint %s", value.c_str());

        _device.remove_endpoint(value);
    });

    get_mqtt_connection().register_callback("audio_config", [this](const string& value) {
        ESP_LOGI(TAG, "Received audio configuration %s", value.c_str());

        AudioConfiguration config;
        if (parse_audio_configuration(value, config)) {
            _device.set_audio_configuration(config);
        } else {
            ESP_LOGE(TAG, "Failed to parse audio configuration");
        }
    });
}

void Application::do_network_available() {
    _udp_server.begin();

    _device.on_state_changed([this]() { state_changed(); });
    _device.begin();
}

void Application::do_ready() {
    // Enable the buttons.
    _controls.set_enabled(true);
    _controls.set_red_runner(new LedOffRunner());
}

void Application::do_process() { _controls.update(); }

void Application::state_changed() {
    if (!get_mqtt_connection().is_connected()) {
        return;
    }

    get_mqtt_connection().send_state(_device.get_state());
}

bool Application::parse_audio_configuration(const string& json, AudioConfiguration& config) {
    cJSON_Data root = {cJSON_Parse(json.c_str())};
    if (!*root) {
        return false;
    }

    auto item = cJSON_GetObjectItem(*root, "volume_scale_low");
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    config.volume_scale_low = (float)item->valuedouble;

    item = cJSON_GetObjectItem(*root, "volume_scale_high");
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    config.volume_scale_high = (float)item->valuedouble;

    item = cJSON_GetObjectItem(*root, "enable_audio_processing");
    if (!cJSON_IsBool(item)) {
        return false;
    }
    config.enable_audio_processing = cJSON_IsTrue(item);

    item = cJSON_GetObjectItem(*root, "audio_buffer_ms");
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    config.audio_buffer_ms = (uint32_t)item->valueint;

    item = cJSON_GetObjectItem(*root, "microphone_gain_bits");
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    config.microphone_gain_bits = (uint8_t)item->valueint;

    item = cJSON_GetObjectItem(*root, "recording_auto_volume_enabled");
    if (!cJSON_IsBool(item)) {
        return false;
    }
    config.recording_auto_volume_enabled = cJSON_IsTrue(item);

    item = cJSON_GetObjectItem(*root, "recording_smoothing_factor");
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    config.recording_smoothing_factor = (float)item->valuedouble;

    item = cJSON_GetObjectItem(*root, "playback_auto_volume_enabled");
    if (!cJSON_IsBool(item)) {
        return false;
    }
    config.playback_auto_volume_enabled = cJSON_IsTrue(item);

    item = cJSON_GetObjectItem(*root, "playback_target_db");
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    config.playback_target_db = (float)item->valuedouble;

    return true;
}

LedAction* Application::parse_led_action(const string& data) {
    LedState state;
    int duration = -1;
    int on = -1;
    int off = -1;

    cJSON_Data root = {cJSON_Parse(data.c_str())};
    if (!*root) {
        ESP_LOGE(TAG, "Failed to parse led action JSON");
        return nullptr;
    }

    auto state_item = cJSON_GetObjectItemCaseSensitive(*root, "state");
    if (cJSON_IsString(state_item) && state_item->valuestring) {
        string state_str(state_item->valuestring);
        if (state_str == "on") {
            state = LedState::On;
        } else if (state_str == "off") {
            state = LedState::Off;
        } else if (state_str == "blink") {
            state = LedState::Blink;
        } else {
            ESP_LOGE(TAG, "Unknown led state %s", state_str.c_str());
            return nullptr;
        }
    } else {
        ESP_LOGE(TAG, "Missing led state");
        return nullptr;
    }

    auto duration_item = cJSON_GetObjectItemCaseSensitive(*root, "duration");
    if (cJSON_IsNumber(duration_item)) {
        duration = duration_item->valueint;
    }

    auto on_item = cJSON_GetObjectItemCaseSensitive(*root, "on");
    if (cJSON_IsNumber(on_item)) {
        on = on_item->valueint;
    }

    auto off_item = cJSON_GetObjectItemCaseSensitive(*root, "off");
    if (cJSON_IsNumber(off_item)) {
        off = off_item->valueint;
    }

    return new LedAction{.state = state, .duration = duration, .on = on, .off = off};
}