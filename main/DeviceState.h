#pragma once

#include <string>
#include <vector>

struct DeviceState {
    bool enabled{};
    bool red_led{};
    bool green_led{};
    bool playing{};
    bool recording{};
    vector<string> subscribed_streams;
};
