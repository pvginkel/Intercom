#pragma once

struct AudioConfiguration {
    float volume_scale_low{};
    float volume_scale_high{};
    bool enable_audio_processing{};
    uint32_t audio_buffer_ms{};
    uint8_t microphone_gain_bits{};
    bool recording_auto_volume_enabled{};
    float recording_smoothing_factor{};
    bool playback_auto_volume_enabled{};
    float playback_target_db{};
};
