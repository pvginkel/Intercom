#pragma once

#include "LedRunner.h"

enum class LedState { On, Off, Blink };

struct LedAction {
    LedState state;
    int duration;
    int on;
    int off;
};
