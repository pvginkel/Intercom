#pragma once

class RingBuffer {
    void* _buffer{};
    size_t _buffer_len{};
    size_t _buffer_offset{};
    size_t _buffer_available{};

public:
    RingBuffer() {}
    ~RingBuffer();

    void initialize(size_t buffer_len);
    void reset();
    void write(void* buffer, size_t buffer_len);
    size_t read(void* buffer, size_t buffer_len);
    size_t skip(size_t len);
    size_t available() { return _buffer_available; }
};
