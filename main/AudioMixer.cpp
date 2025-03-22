#include "includes.h"

#include "AudioMixer.h"

LOG_TAG(AudioMixer);

AudioMixer::AudioMixer() {
    _audio_buffer_len = CONFIG_DEVICE_AUDIO_BUFFER_LEN;
    _buffer_len = _audio_buffer_len * 2;
    _buffer = (uint8_t*)malloc(_buffer_len);
}

AudioMixer::~AudioMixer() { free(_buffer); }

bool AudioMixer::has_data() { return _write_offsets.size() > 0; }

void AudioMixer::reset() {
    _read_offset = 0;
    _write_offsets.clear();

    memset(_buffer, 0, _buffer_len);
}

void AudioMixer::append(const string& topic, uint8_t* buffer, size_t buffer_len) {
    // If we don't have a write offset for this topic, we need to
    // start buffering.

    size_t write_offset;
    if (auto entry = _write_offsets.find(topic); entry != _write_offsets.end()) {
        write_offset = entry->second;
    } else {
        write_offset = _read_offset + _audio_buffer_len;
    }

    auto available = _buffer_len - (write_offset - _read_offset);
    if (available <= 0) {
        ESP_LOGD(TAG, "Dropping incoming sample");
        return;
    }

    ESP_ERROR_ASSERT(available > 0 && available <= _buffer_len);
    auto copy = min(available, buffer_len);
    ESP_ERROR_ASSERT(copy > 0 && copy <= buffer_len);
    auto buffer_offset = buffer_len - copy;

    auto write_offset_mod = write_offset % _buffer_len;

    auto chunk1 = min(_buffer_len - write_offset_mod, copy);
    ESP_ERROR_ASSERT(chunk1 > 0 && chunk1 <= buffer_len + buffer_offset);
    ESP_ERROR_ASSERT(chunk1 > 0 && chunk1 <= _buffer_len + write_offset_mod);

    mix_audio(buffer + buffer_offset, _buffer + write_offset_mod, chunk1);

    if (chunk1 < copy) {
        auto chunk2 = copy - chunk1;

        ESP_ERROR_ASSERT(chunk2 > 0 && chunk2 <= buffer_len);
        ESP_ERROR_ASSERT(chunk2 > 0 && chunk2 <= _buffer_len);

        mix_audio(buffer + buffer_offset + chunk1, _buffer, chunk2);
    }

    _write_offsets[topic] = write_offset + copy;
}

void AudioMixer::mix_audio(uint8_t* source, uint8_t* target, size_t len) {
    for (int i = 0; i < len; i += 2) {
        auto source_sample = (int32_t)(source[i] | (source[i + 1] << 8));
        auto target_sample = (int32_t)(target[i] | (target[i + 1] << 8));

        auto mixed = clamp<int32_t>(target_sample + source_sample, INT16_MIN, INT16_MAX);

        target[i] = (uint8_t)mixed;
        target[i + 1] = (uint8_t)(mixed >> 8);
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

    erase_if(_write_offsets, [this](const auto& pair) { return pair.second < _read_offset; });
}
