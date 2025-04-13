#pragma once

#include "Controls.h"
#include "DeviceState.h"
#include "I2SPlaybackDevice.h"
#include "I2SRecordingDevice.h"
#include "MQTTConnection.h"
#include "UDPServer.h"

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

public:
    Device(MQTTConnection& mqtt_connection, UDPServer& udp_server, Controls& controls);

    void begin();

private:
    void state_changed();
    void load_state();
    void save_state();
    void send_audio(Span<uint8_t> data);
};
