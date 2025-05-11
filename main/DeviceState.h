#pragma once

#include <vector>

#include "AudioConfiguration.h"

struct DeviceState {
    bool enabled{};
    bool red_led{};
    bool green_led{};
    bool playing{};
    bool recording{};
    float volume{};
    AudioConfiguration audio_config{};
};
