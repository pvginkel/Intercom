#pragma once

#include <set>

#include "Callback.h"
#include "DeviceAction.h"
#include "DeviceConfiguration.h"
#include "DeviceState.h"
#include "LedAction.h"
#include "Queue.h"
#include "Span.h"
#include "mqtt_client.h"

struct MQTTConnectionState {
    bool connected;
};

class MQTTConnection {
    static constexpr double DEFAULT_SETPOINT = 19;

    static string get_device_id();

    Queue *_queue;
    string _device_id;
    string _udp_endpoint;
    DeviceConfiguration *_configuration;
    string _topic_prefix;
    esp_mqtt_client_handle_t _client;
    Callback<MQTTConnectionState> _connected_changed;
    Callback<bool> _enabled_changed;
    Callback<bool> _recording_changed;
    Callback<LedAction *> _red_led_changed;
    Callback<LedAction *> _green_led_changed;
    Callback<const string &> _remote_endpoint_added;
    Callback<const string &> _remote_endpoint_removed;
    Callback<float> _volume_changed;
    Callback<void> _identify_requested;

public:
    MQTTConnection(Queue *queue);

    void set_configuration(DeviceConfiguration *configuration) { _configuration = configuration; }
    void set_udp_endpoint(const string &udp_endpoint) { _udp_endpoint = udp_endpoint; }
    void begin();
    void send_state(DeviceState &state);
    void send_action(DeviceAction action);
    void on_connected_changed(function<void(MQTTConnectionState)> func) { _connected_changed.add(func); }
    void on_enabled_changed(function<void(bool)> func) { _enabled_changed.add(func); }
    void on_recording_changed(function<void(bool)> func) { _recording_changed.add(func); }
    void on_red_led_changed(function<void(LedAction *)> func) { _red_led_changed.add(func); }
    void on_green_led_changed(function<void(LedAction *)> func) { _green_led_changed.add(func); }
    void on_remote_endpoint_added(function<void(const string &)> func) { _remote_endpoint_added.add(func); }
    void on_remote_endpoint_removed(function<void(const string &)> func) { _remote_endpoint_removed.add(func); }
    void on_volume_changed(function<void(float)> func) { _volume_changed.add(func); }
    void on_identify_requested(function<void()> func) { _identify_requested.add(func); }

private:
    void event_handler(esp_event_base_t eventBase, int32_t eventId, void *eventData);
    void handle_connected();
    void handle_data(esp_mqtt_event_handle_t event);
    void subscribe(const string &topic);
    void unsubscribe(const string &topic);
    void publish_configuration();
    LedAction *parse_led_action(const string &data);
};
