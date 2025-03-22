#include "includes.h"

#include "RingBuffer.h"

RingBuffer::RingBuffer(size_t buffer_len) {
    _buffer_len = buffer_len;
    _buffer = (uint8_t *)malloc(_buffer_len);
}

RingBuffer::~RingBuffer() { free(_buffer); }

void RingBuffer::reset() {
    _buffer_offset = 0;
    _buffer_available = 0;
}

size_t RingBuffer::write(uint8_t *buffer, size_t buffer_len) {
    auto write_available = _buffer_len - _buffer_available;
    auto write = min(write_available, buffer_len);

    auto write_offset = (_buffer_offset + _buffer_available) % _buffer_len;
    auto write1 = min(write, _buffer_len - write_offset);

    memcpy(_buffer + write_offset, buffer, write1);

    if (write1 < write) {
        auto write2 = write - write1;

        memcpy(_buffer, buffer + write1, write2);
    }

    _buffer_available += write;

    return write;
}

size_t RingBuffer::read(uint8_t *buffer, size_t buffer_len) {
    auto read = min(min(buffer_len, _buffer_len), _buffer_available);
    if (!read) {
        return 0;
    }

    auto read1 = min(_buffer_len - _buffer_offset, read);

    memcpy(buffer, _buffer + _buffer_offset, read1);

    if (read1 < read) {
        auto read2 = read - read1;

        memcpy(buffer + read1, _buffer, read2);
    }

    _buffer_offset = (_buffer_offset + read) % _buffer_len;
    _buffer_available -= read;

    return read;
}
