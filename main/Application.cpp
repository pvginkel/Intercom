#include "support.h"

#include "Application.h"

#include "Messages.h"
#include "driver/i2c.h"
#include "nvs_flash.h"

LOG_TAG(Application);

Application::Application()
    : _network_connection(&_queue, CONFIG_DEVICE_NETWORK_CONNECT_ATTEMPTS),
      _mqtt_connection(&_queue),
      _controls(&_queue),
      _device(_mqtt_connection, _controls) {}

void Application::begin(bool silent) {
    ESP_LOGI(TAG, "Setting up the log manager");

    _log_manager.begin();

    setup_flash();

    do_begin(silent);
}

void Application::setup_flash() {
    ESP_LOGI(TAG, "Setting up flash");

    auto ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void Application::do_begin(bool silent) { begin_network(); }

void Application::begin_network() {
    ESP_LOGI(TAG, "Connecting to WiFi");

    _network_connection.on_state_changed([this](auto state) {
        if (state.connected) {
            begin_network_available();
        } else {
            ESP_LOGE(TAG, "Failed to connect to WiFi; restarting");
            esp_restart();
        }
    });

    _network_connection.begin(CONFIG_WIFI_PASSWORD);
}

void Application::begin_network_available() {
    ESP_LOGI(TAG, "Getting device configuration");

    auto err = _configuration.load();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get configuration; restarting");
        esp_restart();
        return;
    }

    _log_manager.set_device_entity_id(strdup(_configuration.get_device_entity_id().c_str()));

    if (_configuration.get_enable_ota()) {
        _ota_manager.begin();
    }

    begin_mqtt();
}

void Application::begin_mqtt() {
    ESP_LOGI(TAG, "Connecting to MQTT");

    _mqtt_connection.on_connected_changed([this](auto state) {
        if (state.connected) {
            _queue.enqueue([this]() { begin_after_initialization(); });
        } else {
            ESP_LOGE(TAG, "MQTT connection lost");
            esp_restart();
        }
    });

    _mqtt_connection.set_configuration(&_configuration);
    _mqtt_connection.begin();
}

void Application::begin_after_initialization() {
    // Log the reset reason.

    auto reset_reason = esp_reset_reason();
    ESP_LOGI(TAG, "esp_reset_reason: %s (%d)", esp_reset_reason_to_name(reset_reason), reset_reason);

    // Enable the buttons.
    _controls.begin();

    begin_app();
}

void Application::begin_app() {
    ESP_LOGI(TAG, "Startup complete");

    _controls.begin();

    _device.begin();
}

void Application::process() {
    _queue.process();
    _controls.update();
}
