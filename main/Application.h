#pragma once

#include "Controls.h"
#include "Device.h"
#include "DeviceConfiguration.h"
#include "LogManager.h"
#include "MQTTConnection.h"
#include "NetworkConnection.h"
#include "OTAManager.h"
#include "Queue.h"
#include "UDPServer.h"

class Application {
    NetworkConnection _network_connection;
    MQTTConnection _mqtt_connection;
    UDPServer _udp_server;
    Controls _controls;
    Device _device;
    OTAManager _ota_manager;
    Queue _queue;
    DeviceConfiguration _configuration;
    LogManager _log_manager;

public:
    Application();

    void begin(bool silent);
    void process();

private:
    void setup_flash();
    void do_begin(bool silent);
    void begin_network();
    void begin_network_available();
    void begin_after_initialization();
};
