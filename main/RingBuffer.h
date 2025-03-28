#pragma once

class RingBuffer {
    void* _buffer{};
    size_t _buffer_len{};
    size_t _buffer_offset{};
    size_t _buffer_available{};

public:
    RingBuffer(size_t buffer_len);
    ~RingBuffer();

    void reset();
    size_t write(void* buffer, size_t buffer_len);
    size_t read(void* buffer, size_t buffer_len);
};
