#pragma once

#include "Controls.h"
#include "DeviceState.h"
#include "I2SPlaybackDevice.h"
#include "I2SRecordingDevice.h"
#include "MQTTConnection.h"

class Device {
    MQTTConnection& _mqtt_connection;
    Controls& _controls;
    DeviceState _state;
    I2SRecordingDevice _recording_device;
    I2SPlaybackDevice _playback_device;

public:
    Device(MQTTConnection& mqtt_connection, Controls& controls);

    void begin();

private:
    void state_changed();
    void load_state();
    void save_state();
};
