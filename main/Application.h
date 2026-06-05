#pragma once

#include "ApplicationBase.h"
#include "Controls.h"
#include "Device.h"
#include "UDPServer.h"

class Application : public ApplicationBase {
    UDPServer _udp_server;
    Controls _controls;
    Device _device;

public:
    Application();

protected:
    void do_begin() override;
    void do_ready() override;
    void do_process() override;
    void do_network_available() override;
    MQTTDeviceConfiguration get_device_configuration() override;
    int8_t get_wifi_max_tx_power() override;

private:
    void state_changed();
    void register_mqtt_callbacks();
    bool parse_audio_configuration(const string& json, AudioConfiguration& config);
    LedAction* parse_led_action(const string& data);
};
