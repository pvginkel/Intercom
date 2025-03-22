#include "includes.h"

#include "MQTTConnection.h"

LOG_TAG(MQTTConnection);

#define TOPIC_PREFIX "intercom/client"
#define DEVICE_MANUFACTURER "Pieter"
#define DEVICE_MODEL "Intercom"

#define LAST_WILL_MESSAGE "{\"online\": false}"

#define QOS_MAX_ONE 0      // Send at most one.
#define QOS_MIN_ONE 1      // Send at least one.
#define QOS_EXACTLY_ONE 2  // Send exactly one.

MQTTConnection::MQTTConnection(Queue *queue) : _queue(queue), _device_id(get_device_id()) {}

void MQTTConnection::begin() {
    esp_log_level_set("mqtt5_client", ESP_LOG_WARN);

    esp_mqtt5_connection_property_config_t connect_property = {
        .session_expiry_interval = 10,
        .maximum_packet_size = 1024,
        .receive_maximum = 65535,
        .topic_alias_maximum = 2,
        .request_resp_info = true,
        .request_problem_info = true,
        .will_delay_interval = 10,
        .message_expiry_interval = 10,
        .payload_format_indicator = true,
    };

    _topic_prefix = TOPIC_PREFIX "/" + _device_id + "/";

    const auto state_topic = _topic_prefix + "state";

    esp_mqtt_client_config_t config = {
        .broker =
            {
                .address =
                    {
                        .uri = CONFIG_MQTT_BROKER_URL,
                    },
            },
        .session =
            {
                .last_will =
                    {
                        .topic = state_topic.c_str(),
                        .msg = LAST_WILL_MESSAGE,
                        .msg_len = strlen(LAST_WILL_MESSAGE),
                        .qos = QOS_MIN_ONE,
                        .retain = true,
                    },
                .protocol_ver = MQTT_PROTOCOL_V_5,
            },
        .network =
            {
                .disable_auto_reconnect = false,
            },
    };

    if (CONFIG_MQTT_USER_ID && *CONFIG_MQTT_USER_ID) {
        config.credentials.username = CONFIG_MQTT_USER_ID;
        config.credentials.authentication.password = CONFIG_MQTT_PASSWORD;
    }

    _client = esp_mqtt_client_init(&config);

    esp_mqtt5_client_set_connect_property(_client, &connect_property);

    esp_mqtt_client_register_event(
        _client, MQTT_EVENT_ANY,
        [](auto eventHandlerArg, auto eventBase, auto eventId, auto eventData) {
            ((MQTTConnection *)eventHandlerArg)->event_handler(eventBase, eventId, eventData);
        },
        this);

    esp_mqtt_client_start(_client);
}

string MQTTConnection::get_device_id() {
    uint8_t mac[6];

    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));

    return strformat("0x%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void MQTTConnection::event_handler(esp_event_base_t eventBase, int32_t eventId, void *eventData) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, eventBase, eventId);
    auto event = (esp_mqtt_event_handle_t)eventData;

    ESP_LOGD(TAG, "Free heap size is %" PRIu32 ", minimum %" PRIu32, esp_get_free_heap_size(),
             esp_get_minimum_free_heap_size());

    switch ((esp_mqtt_event_id_t)eventId) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            handle_connected();
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");

            _connected_changed.queue(_queue, {false});
            break;

        case MQTT_EVENT_SUBSCRIBED:
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            break;

        case MQTT_EVENT_PUBLISHED:
            break;

        case MQTT_EVENT_DATA:
            handle_data(event);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT return code is %d", event->error_handle->connect_return_code);
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                if (event->error_handle->esp_tls_last_esp_err) {
                    ESP_LOGI(TAG, "reported from esp-tls");
                }
                if (event->error_handle->esp_tls_stack_err) {
                    ESP_LOGI(TAG, "reported from tls stack");
                }
                if (event->error_handle->esp_transport_sock_errno) {
                    ESP_LOGI(TAG, "captured as transport's socket errno");
                }
                ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;

        default:
            ESP_LOGD(TAG, "Other event id: %d", event->event_id);
            break;
    }
}

void MQTTConnection::handle_connected() {
    const auto stream_in_topic = _topic_prefix + "stream/in";

    subscribe(_topic_prefix + "set/+");
    subscribe(stream_in_topic);

    _subscribed_streams.insert(stream_in_topic);

    publish_configuration();

    _connected_changed.queue(_queue, {true});
}

void MQTTConnection::handle_data(esp_mqtt_event_handle_t event) {
    auto topic = string(event->topic, event->topic_len);

    if (_subscribed_streams.contains(topic)) {
        _stream_data.call(MQTTStreamData(topic, {(uint8_t *)event->data, (size_t)event->data_len}));
        return;
    }

    if (!topic.starts_with(_topic_prefix)) {
        ESP_LOGE(TAG, "Unexpected topic %s", topic.c_str());
        return;
    }

    auto sub_topic = topic.c_str() + _topic_prefix.length();
    auto data = string(event->data, event->data_len);

    if (strcmp(sub_topic, "set/enabled") == 0) {
        ESP_LOGI(TAG, "Setting enabled to %s", data.c_str());

        _enabled_changed.call(iequals(data, "true"));
    } else if (strcmp(sub_topic, "set/recording") == 0) {
        ESP_LOGI(TAG, "Setting recording to %s", data.c_str());

        _recording_changed.call(iequals(data, "true"));
    } else if (strcmp(sub_topic, "set/red_led") == 0) {
        ESP_LOGI(TAG, "Setting red led to %s", data.c_str());

        if (auto action = parse_led_action(data)) {
            _red_led_changed.queue(_queue, action);
        }
    } else if (strcmp(sub_topic, "set/green_led") == 0) {
        ESP_LOGI(TAG, "Setting green led to %s", data.c_str());

        if (auto action = parse_led_action(data)) {
            _green_led_changed.queue(_queue, action);
        }
    } else if (strcmp(sub_topic, "set/subscribe_stream") == 0) {
        ESP_LOGI(TAG, "Subscribing to topic %s", data.c_str());

        _subscribed_streams.insert(data);
        _stream_subscribed.call(data);

        subscribe(data);
    } else if (strcmp(sub_topic, "set/unsubscribe_stream") == 0) {
        ESP_LOGI(TAG, "Unsubscribing from topic %s", data.c_str());

        _subscribed_streams.erase(data);
        _stream_unsubscribed.call(data);

        unsubscribe(data);
    } else {
        ESP_LOGE(TAG, "Unknown topic %s", topic.c_str());
    }
}

void MQTTConnection::subscribe(const string &topic) {
    ESP_LOGI(TAG, "Subscribing to topic %s", topic.c_str());

    ESP_ERROR_ASSERT(esp_mqtt_client_subscribe(_client, topic.c_str(), 0) >= 0);
}

void MQTTConnection::unsubscribe(const string &topic) {
    ESP_LOGI(TAG, "Unsubscribing to topic %s", topic.c_str());

    ESP_ERROR_ASSERT(esp_mqtt_client_unsubscribe(_client, topic.c_str()) >= 0);
}

void MQTTConnection::publish_configuration() {
    ESP_LOGI(TAG, "Publishing configuration information");

    auto uniqueIdentifier = strformat("%s_%s", TOPIC_PREFIX, _device_id.c_str());

    auto root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "unique_id", uniqueIdentifier.c_str());

    auto audio_formats = cJSON_AddObjectToObject(root, "audio_formats");

    auto audio_formats_in = cJSON_AddObjectToObject(audio_formats, "in");
    cJSON_AddStringToObject(audio_formats_in, "channel_layout", "mono");
    cJSON_AddNumberToObject(audio_formats_in, "sample_rate", CONFIG_DEVICE_I2S_SAMPLE_RATE);
    cJSON_AddNumberToObject(audio_formats_in, "bit_rate", CONFIG_DEVICE_I2S_BITS_PER_SAMPLE);

    auto audio_formats_out = cJSON_AddObjectToObject(audio_formats, "out");
    cJSON_AddStringToObject(audio_formats_out, "channel_layout", "mono");
    cJSON_AddNumberToObject(audio_formats_out, "sample_rate", CONFIG_DEVICE_I2S_SAMPLE_RATE);
    cJSON_AddNumberToObject(audio_formats_out, "bit_rate", CONFIG_DEVICE_I2S_BITS_PER_SAMPLE);

    auto device = cJSON_AddObjectToObject(root, "device");
    cJSON_AddStringToObject(device, "manufacturer", DEVICE_MANUFACTURER);
    cJSON_AddStringToObject(device, "model", DEVICE_MODEL);
    cJSON_AddStringToObject(device, "name", _configuration->get_device_name().c_str());

    auto json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    auto topic = _topic_prefix + "configuration";
    ESP_ERROR_ASSERT(esp_mqtt_client_publish(_client, topic.c_str(), json, 0, QOS_MIN_ONE, true) >= 0);

    cJSON_free(json);
}

LedAction *MQTTConnection::parse_led_action(const string &data) {
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

void MQTTConnection::send_state(DeviceState &state) {
    ESP_LOGI(TAG, "Publishing new state");

    cJSON_Data root = {cJSON_CreateObject()};

    cJSON_AddBoolToObject(*root, "online", true);
    cJSON_AddBoolToObject(*root, "enabled", state.enabled);
    cJSON_AddBoolToObject(*root, "red_led", state.red_led);
    cJSON_AddBoolToObject(*root, "green_led", state.green_led);
    cJSON_AddBoolToObject(*root, "playing", state.playing);
    cJSON_AddBoolToObject(*root, "recording", state.recording);

    auto subscribed_streams = cJSON_AddArrayToObject(*root, "subscribed_streams");

    for (const auto &subscribed_stream : state.subscribed_streams) {
        cJSON_AddItemToArray(subscribed_streams, cJSON_CreateString(subscribed_stream.c_str()));
    }

    auto json = cJSON_PrintUnformatted(*root);

    auto topic = _topic_prefix + "state";
    auto result = esp_mqtt_client_publish(_client, topic.c_str(), json, 0, QOS_MIN_ONE, true);
    if (result < 0) {
        ESP_LOGE(TAG, "Sending status update message failed with error %d", result);
    }

    cJSON_free(json);
}

void MQTTConnection::send_action(DeviceAction action) {
    const auto data = action == DeviceAction::Click ? "click" : "long_click";

    auto topic = _topic_prefix + "set/action";
    auto result = esp_mqtt_client_publish(_client, topic.c_str(), data, 0, QOS_MIN_ONE, false);
    if (result < 0) {
        ESP_LOGE(TAG, "Sending action message failed with error %d", result);
    }
}

void MQTTConnection::send_audio(Span<uint8_t> data) {
    auto topic = _topic_prefix + "stream/out";
    auto result =
        esp_mqtt_client_publish(_client, topic.c_str(), (const char *)data.buffer(), data.len(), QOS_MIN_ONE, false);
    if (result < 0) {
        ESP_LOGE(TAG, "Sending action message failed with error %d", result);
    }
}
