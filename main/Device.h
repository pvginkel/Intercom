#pragma once

#include "Controls.h"
#include "DeviceState.h"
#include "I2SPlaybackDevice.h"
#include "I2SRecordingDevice.h"
#include "MQTTConnection.h"
#include "UDPServer.h"

enum class DeviceAction { Click, LongClick };

class Device {
    struct Endpoint {
        string endpoint;
        sockaddr_in addr;
    };

    MQTTConnection& _mqtt_connection;
    UDPServer& _udp_server;
    Controls& _controls;
    DeviceState _state;
    I2SRecordingDevice _recording_device;
    I2SPlaybackDevice _playback_device;
    vector<Endpoint> _remote_endpoints;
    uint8_t* _send_buffer;
    int32_t _next_packet_index{};
    Callback<void> _state_changed;

public:
    Device(MQTTConnection& mqtt_connection, UDPServer& udp_server, Controls& controls);

    void begin();
    void identify();
    void set_volume(float volume);
    void set_enabled(bool enabled);
    void set_recording(bool recording);
    void set_red_led(LedAction* action);
    void set_green_led(LedAction* action);
    void add_endpoint(const string& endpoint);
    void remove_endpoint(const string& endpoint);
    void set_audio_configuration(const AudioConfiguration& config);
    void on_state_changed(function<void()> func) { _state_changed.add(func); }
    cJSON* get_state();

private:
    void send_action(DeviceAction action);
    void state_changed();
    void load_state();
    void save_state();
    void send_audio(Span<uint8_t> data);
};
