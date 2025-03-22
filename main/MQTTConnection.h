#pragma once

#include <set>
#include <string>

#include "Callback.h"
#include "DeviceAction.h"
#include "DeviceConfiguration.h"
#include "DeviceState.h"
#include "LedAction.h"
#include "Queue.h"
#include "Span.h"

struct MQTTConnectionState {
    bool connected;
};

struct MQTTStreamData {
    MQTTStreamData(const string &topic, Span<uint8_t> buffer) : topic(topic), buffer(buffer) {}

    const string &topic;
    Span<uint8_t> buffer;
};

class MQTTConnection {
    static constexpr double DEFAULT_SETPOINT = 19;

    static string get_device_id();

    Queue *_queue;
    DeviceConfiguration *_configuration;
    string _device_id;
    string _topic_prefix;
    esp_mqtt_client_handle_t _client;
    Callback<MQTTConnectionState> _connected_changed;
    Callback<const MQTTStreamData &> _stream_data;
    Callback<bool> _enabled_changed;
    Callback<bool> _recording_changed;
    Callback<LedAction *> _red_led_changed;
    Callback<LedAction *> _green_led_changed;
    Callback<const string &> _stream_subscribed;
    Callback<const string &> _stream_unsubscribed;
    set<string> _subscribed_streams;

public:
    MQTTConnection(Queue *queue);

    void set_configuration(DeviceConfiguration *configuration) { _configuration = configuration; }
    void begin();
    void send_state(DeviceState &state);
    void send_action(DeviceAction action);
    void send_audio(Span<uint8_t> data);
    void on_connected_changed(function<void(MQTTConnectionState)> func) { _connected_changed.add(func); }
    void on_stream_data(function<void(const MQTTStreamData &)> func) { _stream_data.add(func); }
    void on_enabled_changed(function<void(bool)> func) { _enabled_changed.add(func); }
    void on_recording_changed(function<void(bool)> func) { _recording_changed.add(func); }
    void on_red_led_changed(function<void(LedAction *)> func) { _red_led_changed.add(func); }
    void on_green_led_changed(function<void(LedAction *)> func) { _green_led_changed.add(func); }
    void on_stream_subscribed(function<void(const string &)> func) { _stream_subscribed.add(func); }
    void on_stream_unsubscribed(function<void(const string &)> func) { _stream_unsubscribed.add(func); }

private:
    void event_handler(esp_event_base_t eventBase, int32_t eventId, void *eventData);
    void handle_connected();
    void handle_data(esp_mqtt_event_handle_t event);
    void subscribe(const string &topic);
    void unsubscribe(const string &topic);
    void publish_configuration();
    LedAction *parse_led_action(const string &data);
};
