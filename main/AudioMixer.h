#pragma once

#include <map>

class AudioMixer {
    size_t _audio_buffer_len;
    uint8_t* _buffer{};
    size_t _buffer_len{};
    size_t _read_offset;
    map<string, size_t> _write_offsets;

public:
    AudioMixer();
    ~AudioMixer();

    bool has_data();
    void reset();
    void append(const string& topic, uint8_t* buffer, size_t buffer_len);
    void take(uint8_t* buffer, size_t buffer_len);

private:
    void mix_audio(uint8_t* source, uint8_t* target, size_t len);
};
