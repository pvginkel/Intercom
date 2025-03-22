#pragma once

class RingBuffer {
    uint8_t* _buffer{};
    size_t _buffer_len{};
    size_t _buffer_offset{};
    size_t _buffer_available{};

public:
    RingBuffer(size_t buffer_len);
    ~RingBuffer();

    void reset();
    size_t write(uint8_t* buffer, size_t buffer_len);
    size_t read(uint8_t* buffer, size_t buffer_len);
};
