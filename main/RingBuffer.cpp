#include "support.h"

#include "RingBuffer.h"

LOG_TAG(RingBuffer);

RingBuffer::~RingBuffer() { free(_buffer); }

void RingBuffer::initialize(size_t buffer_len) {
    _buffer_len = buffer_len;
    _buffer = heap_caps_malloc(_buffer_len, MALLOC_CAP_INTERNAL);
    ESP_ERROR_ASSERT(_buffer);
}

void RingBuffer::reset() {
    _buffer_offset = 0;
    _buffer_available = 0;
}

void RingBuffer::write(void *buffer, size_t buffer_len) {
    ESP_ERROR_ASSERT(buffer_len <= _buffer_len);

    auto write_available = _buffer_len - _buffer_available;
    if (buffer_len > write_available) {
        auto overflow = buffer_len - write_available;

        _buffer_offset = (_buffer_offset + overflow) % _buffer_len;
        _buffer_available -= overflow;
    }

    auto write_offset = (_buffer_offset + _buffer_available) % _buffer_len;
    auto write1 = min(buffer_len, _buffer_len - write_offset);

    memcpy((uint8_t *)_buffer + write_offset, buffer, write1);

    if (write1 < buffer_len) {
        auto write2 = buffer_len - write1;

        memcpy(_buffer, (uint8_t *)buffer + write1, write2);
    }

    _buffer_available += buffer_len;
}

size_t RingBuffer::read(void *buffer, size_t buffer_len) {
    auto read = min(min(buffer_len, _buffer_len), _buffer_available);
    if (!read) {
        return 0;
    }

    auto read1 = min(_buffer_len - _buffer_offset, read);

    memcpy(buffer, (uint8_t *)_buffer + _buffer_offset, read1);

    if (read1 < read) {
        auto read2 = read - read1;

        memcpy((uint8_t *)buffer + read1, _buffer, read2);
    }

    _buffer_offset = (_buffer_offset + read) % _buffer_len;
    _buffer_available -= read;

    return read;
}

size_t RingBuffer::skip(size_t len) {
    auto skip = min(len, _buffer_available);
    if (!skip) {
        return 0;
    }

    _buffer_offset = (_buffer_offset + skip) % _buffer_len;
    _buffer_available -= skip;

    return skip;
}
