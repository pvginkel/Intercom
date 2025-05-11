#include "support.h"

#include "AudioMixer.h"

#include <algorithm>

LOG_TAG(AudioMixer);

AudioMixer::~AudioMixer() { free(_buffer); }

void AudioMixer::initialize(uint32_t buffer_len_ms) {
    _audio_buffer_len = AUDIO_BUFFER_LEN(buffer_len_ms);
    _buffer_len = _audio_buffer_len * 2;
    _buffer = (uint8_t*)heap_caps_malloc(_buffer_len, MALLOC_CAP_INTERNAL);
    ESP_ERROR_ASSERT(_buffer);

    reset();
}

bool AudioMixer::has_data() { return _write_offsets.size() > 0; }

void AudioMixer::reset() {
    _read_offset = 0;
    _write_offsets.clear();

    memset(_buffer, 0, _buffer_len);
}

void AudioMixer::append(sockaddr_in* source_addr, uint8_t* buffer, size_t buffer_len) {
    if (buffer_len < 4) {
        ESP_LOGW(TAG, "Invalid incoming buffer length");
        return;
    }

    // If we don't have a write offset for this topic, we need to
    // start buffering.

    const auto key = make_tuple(source_addr->sin_addr.s_addr, source_addr->sin_port);

    WriteOffset write_offset;
    if (auto entry = _write_offsets.find(key); entry != _write_offsets.end()) {
        write_offset = entry->second;
    } else {
        write_offset = {
            .offset = _read_offset + _audio_buffer_len,
            .packet_index = -1,
        };
    }

    auto packet_index = (int32_t)ntohl(*(uint32_t*)buffer);
    if (packet_index < write_offset.packet_index) {
        ESP_LOGW(TAG, "Dropping incoming packet; packet index %d, write offset packet index %d", (int)packet_index,
                 (int)write_offset.packet_index);
        return;
    }

    buffer += sizeof(int32_t);
    buffer_len -= sizeof(int32_t);

    auto available = _buffer_len - (write_offset.offset - _read_offset);
    if (available <= 0) {
        ESP_LOGW(TAG, "Dropping incoming packet; no buffer available");
        return;
    }
    if (available < buffer_len) {
        ESP_LOGW(TAG, "Dropping part of incoming sample available %d buffer_len %d", (int)available, (int)buffer_len);
    }

    ESP_ERROR_ASSERT(available > 0 && available <= _buffer_len);
    auto copy = min(available, buffer_len);
    ESP_ERROR_ASSERT(copy > 0 && copy <= buffer_len);
    auto buffer_offset = buffer_len - copy;

    auto write_offset_mod = write_offset.offset % _buffer_len;

    auto chunk1 = min(_buffer_len - write_offset_mod, copy);
    ESP_ERROR_ASSERT(chunk1 > 0 && chunk1 <= buffer_len + buffer_offset);
    ESP_ERROR_ASSERT(chunk1 > 0 && chunk1 <= _buffer_len + write_offset_mod);

    mix_audio((int16_t*)(buffer + buffer_offset), (int16_t*)(_buffer + write_offset_mod), chunk1 / sizeof(int16_t));

    if (chunk1 < copy) {
        auto chunk2 = copy - chunk1;

        ESP_ERROR_ASSERT(chunk2 > 0 && chunk2 <= buffer_len);
        ESP_ERROR_ASSERT(chunk2 > 0 && chunk2 <= _buffer_len);

        mix_audio((int16_t*)(buffer + buffer_offset + chunk1), (int16_t*)_buffer, chunk2 / sizeof(int16_t));
    }

    _write_offsets[key] = {
        .offset = write_offset.offset + copy,
        .packet_index = packet_index,
    };
}

void AudioMixer::mix_audio(int16_t* source, int16_t* target, size_t samples) {
    for (int i = 0; i < samples; i++) {
        auto source_sample = (int32_t)source[i];
        auto target_sample = (int32_t)target[i];

        target[i] = clamp<int32_t>(target_sample + source_sample, INT16_MIN, INT16_MAX);
    }
}

void AudioMixer::take(uint8_t* buffer, size_t buffer_len) {
    ESP_ERROR_ASSERT(buffer_len <= _buffer_len);

    auto read_offset_mod = (int)(_read_offset % _buffer_len);
    auto chunk1 = min(_buffer_len - read_offset_mod, buffer_len);

    ESP_ERROR_ASSERT(read_offset_mod >= 0 && read_offset_mod < _buffer_len);
    ESP_ERROR_ASSERT(chunk1 <= buffer_len);
    ESP_ERROR_ASSERT(chunk1 + read_offset_mod <= _buffer_len);

    memcpy(buffer, _buffer + read_offset_mod, chunk1);
    memset(_buffer + read_offset_mod, 0, chunk1);

    if (chunk1 < buffer_len) {
        auto chunk2 = buffer_len - chunk1;

        ESP_ERROR_ASSERT(chunk2 > 0 && chunk2 <= buffer_len && chunk2 <= _buffer_len);

        memcpy(buffer + chunk1, _buffer, chunk2);
        memset(_buffer, 0, chunk2);
    }

    _read_offset += buffer_len;

    // If any of the topics write offsets is less than what we've
    // read, it means we didn't have enough buffered. Start
    // buffering again.

    erase_if(_write_offsets, [this](const auto& pair) { return pair.second.offset < _read_offset; });
}
