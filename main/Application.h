#pragma once

#include "Buttons.h"
#include "DeviceConfiguration.h"
#include "LogManager.h"
#include "NetworkConnection.h"
#include "OTAManager.h"
#include "Queue.h"

class Application {
    NetworkConnection _network_connection;
    OTAManager _ota_manager;
    Queue _queue;
    DeviceConfiguration _configuration;
    LogManager _log_manager;
    Buttons _buttons;

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
    void begin_ui();
};
