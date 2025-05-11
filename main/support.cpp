#include "support.h"

int getisoweek(tm& time_info) {
    char week_str[3];
    strftime(week_str, sizeof(week_str), "%V", &time_info);

    return atoi(week_str);
}

static bool ichar_equals(char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
}

bool iequals(const string& a, const string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), ichar_equals);
}

int hextoi(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    return -1;
}

esp_err_t esp_http_download_string(const esp_http_client_config_t& config, string& target, size_t max_length,
                                   const char* authorization) {
    target.clear();

    constexpr size_t BUFFER_SIZE = 1024;
    const auto bufferSize = max_length > 0 ? min(max_length + 1, BUFFER_SIZE) : BUFFER_SIZE;

    auto buffer = new char[bufferSize];
    auto err = ESP_OK;
    int64_t length = 0;

    auto client = esp_http_client_init(&config);

    if (authorization) {
        esp_http_client_set_header(client, "Authorization", authorization);
    }

    if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
        goto end;
    }

    length = esp_http_client_fetch_headers(client);
    if (length < 0) {
        err = -length;
        goto end;
    }

    while (true) {
        auto read = esp_http_client_read(client, buffer, bufferSize);
        if (read < 0) {
            err = -read;
            goto end;
        }
        if (read == 0) {
            break;
        }

        if (max_length > 0 && target.length() + read > max_length) {
            err = ESP_ERR_INVALID_SIZE;
            goto end;
        }

        target.append(buffer, read);
    }

end:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    delete[] buffer;

    return err;
}

esp_err_t esp_http_upload_string(const esp_http_client_config_t& config, const char* const data) {
    auto err = ESP_OK;

    auto client = esp_http_client_init(&config);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, data, strlen(data));

    err = esp_http_client_perform(client);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return err;
}

char const* esp_reset_reason_to_name(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:
            return "ESP_RST_POWERON";
        case ESP_RST_EXT:
            return "ESP_RST_EXT";
        case ESP_RST_SW:
            return "ESP_RST_SW";
        case ESP_RST_PANIC:
            return "ESP_RST_PANIC";
        case ESP_RST_INT_WDT:
            return "ESP_RST_INT_WDT";
        case ESP_RST_TASK_WDT:
            return "ESP_RST_TASK_WDT";
        case ESP_RST_WDT:
            return "ESP_RST_WDT";
        case ESP_RST_DEEPSLEEP:
            return "ESP_RST_DEEPSLEEP";
        case ESP_RST_BROWNOUT:
            return "ESP_RST_BROWNOUT";
        case ESP_RST_SDIO:
            return "ESP_RST_SDIO";
        default:
            return "ESP_RST_UNKNOWN";
    }
}

esp_err_t parse_endpoint(sockaddr_in* addr, const char* input) {
    const string endpoint(input);

    auto pos = endpoint.find(':');
    if (pos == string::npos) {
        return ESP_ERR_INVALID_ARG;
    }

    auto ip = endpoint.substr(0, pos);
    auto port = stoi(endpoint.substr(pos + 1));

    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &addr->sin_addr) != 1) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}
