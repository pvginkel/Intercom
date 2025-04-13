#pragma once

#include "Callback.h"
#include "Mutex.h"

struct UDPPacket {
    sockaddr_in* source_addr;
    void* buffer;
    size_t buffer_len;
};

class UDPServer {
    Mutex _lock;
    Callback<UDPPacket> _received;
    int _port;
    void* _receive_buffer;
    int _sock{-1};

public:
    static constexpr size_t PAYLOAD_LEN = 1472 /* max safe data size assuming an MTU of 1500 */;

    UDPServer(int port) : _port(port) {}

    void begin();
    int get_port() { return _port; }
    void on_received(function<void(UDPPacket)> func) { _received.add(func); }
    void send(const struct sockaddr* to, socklen_t tolen, void* buffer, size_t buffer_len);

private:
    void receive_loop();
    void run_server();
};
