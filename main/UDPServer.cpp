#include "support.h"

#include "UDPServer.h"

#include "lwip/sockets.h"

LOG_TAG(UDPServer);

void UDPServer::begin() {
    _receive_buffer = malloc(PAYLOAD_LEN);
    ESP_ERROR_ASSERT(_receive_buffer);

    FREERTOS_CHECK(xTaskCreatePinnedToCore(
        [](void *param) {
            ((UDPServer *)param)->receive_loop();

            vTaskDelete(nullptr);
        },
        "udp_server", CONFIG_ESP_MAIN_TASK_STACK_SIZE, this, 5, nullptr, 0));
}

void UDPServer::send(const sockaddr *to, socklen_t tolen, void *buffer, size_t buffer_len) {
    auto guard = _lock.take();

    int err = sendto(_sock, buffer, buffer_len, 0, to, tolen);
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to send packet: errno %d", errno);
    }
}

void UDPServer::receive_loop() {
    while (true) {
        run_server();

        ESP_LOGW(TAG, "Restarting UDP server...");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void UDPServer::run_server() {
    sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(_port),
        .sin_addr =
            {
                .s_addr = htonl(INADDR_ANY),
            },
    };

    _sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (_sock < 0) {
        ESP_LOGE(TAG, "Failed to open socket; error %d", _sock);
        return;
    }

    // Set timeout
    timeval timeout = {.tv_sec = 10};
    setsockopt(_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    int err = bind(_sock, (sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind; errno %d", errno);
        return;
    }

    ESP_LOGI(TAG, "Socket bound on port %d", _port);

    sockaddr_storage source_addr;
    socklen_t socklen = sizeof(source_addr);

    while (true) {
        int len = recvfrom(_sock, _receive_buffer, PAYLOAD_LEN, 0, (sockaddr *)&source_addr, &socklen);
        if (len < 0) {
            if (errno == EAGAIN) {
                continue;
            }

            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            break;
        }

        _received.call(UDPPacket{
            .source_addr = (sockaddr_in *)&source_addr,
            .buffer = _receive_buffer,
            .buffer_len = (size_t)len,
        });
    }

    if (_sock != -1) {
        ESP_LOGE(TAG, "Shutting down socket and restarting...");
        shutdown(_sock, 0);
        close(_sock);
    }
}
