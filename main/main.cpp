#include "support.h"

#include "Application.h"

LOG_TAG(main);

#ifdef CONFIG_DEVICE_SHOW_CPU_USAGE

#ifndef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
#error CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS must be set
#endif
#ifndef CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
#error CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS must be set
#endif

[[maybe_unused]] static void show_task_statistics() {
    FREERTOS_CHECK(xTaskCreatePinnedToCore(
        [](void *param) {
            static auto task_buffer = (char *)malloc(2000);

            while (true) {
                ESP_LOGI(TAG, "Performance statistics");

                vTaskGetRunTimeStats(task_buffer);
                printf(task_buffer);

                vTaskDelay(pdMS_TO_TICKS(5000));
            }
        },
        "top", CONFIG_ESP_MAIN_TASK_STACK_SIZE, nullptr, 5, nullptr, 0));
}

#endif

extern "C" void app_main() {
    // If we've restarted because of a brownout or watchdog reset,
    // perform a silent startup.
    const auto resetReason = esp_reset_reason();
    const auto silent = resetReason == ESP_RST_BROWNOUT || resetReason == ESP_RST_WDT;

#ifdef CONFIG_DEVICE_SHOW_CPU_USAGE
    show_task_statistics();
#endif

    Application application;

    application.begin(silent);

    while (1) {
        application.process();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
