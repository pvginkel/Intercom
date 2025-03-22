#pragma once

#include "cJSON.h"
#include "sdkconfig.h"

#define __CONCAT3(a, b, c) a##b##c
#define CONCAT3(a, b, c) __CONCAT3(a, b, c)

#define CONFIG_DEVICE_I2S_DATA_BIT_WIDTH CONCAT3(I2S_DATA_BIT_WIDTH_, CONFIG_DEVICE_I2S_BITS_PER_SAMPLE, BIT)

#define CONFIG_DEVICE_AUDIO_BUFFER_LEN \
    (CONFIG_DEVICE_I2S_BITS_PER_SAMPLE / 8 * CONFIG_DEVICE_I2S_SAMPLE_RATE * CONFIG_DEVICE_AUDIO_BUFFER_MS / 1000)
#define CONFIG_DEVICE_AUDIO_CHUNK_LEN \
    (CONFIG_DEVICE_I2S_BITS_PER_SAMPLE / 8 * CONFIG_DEVICE_I2S_SAMPLE_RATE * CONFIG_DEVICE_AUDIO_CHUNK_MS / 1000)

#define esp_get_millis() uint32_t(esp_timer_get_time() / 1000ull)

string strformat(const char* fmt, ...);
int getisoweek(tm& time_info);

#ifdef NDEBUG
#define ESP_ERROR_ASSERT(x) \
    do {                    \
        (void)sizeof((x));  \
    } while (0)
#elif defined(CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT)
#define ESP_ERROR_ASSERT(x)   \
    do {                      \
        if (unlikely(!(x))) { \
            abort();          \
        }                     \
    } while (0)
#else
#define ESP_ERROR_ASSERT(x)                                                                                    \
    do {                                                                                                       \
        if (unlikely(!(x))) {                                                                                  \
            printf("ESP_ERROR_ASSERT failed");                                                                 \
            printf(" at %p\n", __builtin_return_address(0));                                                   \
            printf("file: \"%s\" line %d\nfunc: %s\nexpression: %s\n", __FILE__, __LINE__, __ASSERT_FUNC, #x); \
            abort();                                                                                           \
        }                                                                                                      \
    } while (0)
#endif

#define ESP_TIMER_MS(v) ((v) * 1000)
#define ESP_TIMER_SECONDS(v) ESP_TIMER_MS((v) * 1000)

#define ESP_ERROR_CHECK_JUMP(x, label)                                     \
    do {                                                                   \
        esp_err_t err_rc_ = (x);                                           \
        if (unlikely(err_rc_ != ESP_OK)) {                                 \
            ESP_LOGE(TAG, #x " failed with %s", esp_err_to_name(err_rc_)); \
            goto label;                                                    \
        }                                                                  \
    } while (0)

bool iequals(const string& a, const string& b);
int hextoi(char c);

#define LOG_TAG(v) [[maybe_unused]] static const char* TAG = #v

class cJSON_Data {
    cJSON* _data;

public:
    cJSON_Data(cJSON* data) : _data(data) {}
    cJSON_Data(const cJSON_Data& other) = delete;
    cJSON_Data(cJSON_Data&& other) noexcept = delete;
    cJSON_Data& operator=(const cJSON_Data& other) = delete;
    cJSON_Data& operator=(cJSON_Data&& other) noexcept = delete;

    ~cJSON_Data() {
        if (_data) {
            cJSON_Delete(_data);
        }
    }

    cJSON* operator*() const { return _data; }
};

#ifdef LV_SIMULATOR
#define IRAM_ATTR

#define localtime_r(timep, result) localtime_s(result, timep)
#endif

#ifndef LV_SIMULATOR

esp_err_t esp_http_download_string(const esp_http_client_config_t& config, string& target, size_t max_length = 0,
                                   const char* authorization = nullptr);
esp_err_t esp_http_upload_string(const esp_http_client_config_t& config, const char* const data);
char const* esp_reset_reason_to_name(esp_reset_reason_t reason);

#endif
