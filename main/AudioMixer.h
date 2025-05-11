#pragma once

#include <map>
#include <tuple>

class AudioMixer {
    struct WriteOffset {
        size_t offset;
        int32_t packet_index;
    };

    size_t _audio_buffer_len;
    uint8_t* _buffer{};
    size_t _buffer_len{};
    size_t _read_offset;
    map<tuple<in_addr_t, in_port_t>, WriteOffset> _write_offsets;

public:
    AudioMixer() {}
    ~AudioMixer();

    void initialize(uint32_t buffer_len_ms);
    bool has_data();
    void reset();
    void append(sockaddr_in* source_addr, uint8_t* buffer, size_t buffer_len);
    void take(uint8_t* buffer, size_t buffer_len);

private:
    void mix_audio(int16_t* source, int16_t* target, size_t samples);
};
