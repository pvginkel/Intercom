#pragma once

#include <vector>

struct DeviceState {
    bool enabled{};
    bool red_led{};
    bool green_led{};
    bool playing{};
    bool recording{};
};
