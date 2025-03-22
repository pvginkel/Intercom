#pragma once

#define _USE_MATH_DEFINES

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace std;

#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <cctype>
#include <cstdarg>

#include "Callback.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "secrets.h"
#include "support.h"
