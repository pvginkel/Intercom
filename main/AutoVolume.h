#pragma once

#include <vector>

/**
 * One-channel automatic volume control (downward only) with
 *   - look-ahead         (1 ms delay, prevents attack distortion)
 *   - hold time          (stops gain “chatter” between syllables)
 *   - soft knee          (3 dB quadratic knee around the target)
 *   - post limiter       (–0.1 dBFS hard clip, catches residual peaks)
 */
class AutoVolume {
    // Delay applied to audio so gain precedes peaks (ms)
    static constexpr float LOOKAHEAD_MS = 2.0f;

    // Attack time (ms)  – how fast attenuation ramps down
    static constexpr float ATTACK = MS2S(10.0f);
    // Release time (ms) – how fast it recovers
    static constexpr float RELEASE = MS2S(150.0f);
    // RMS window for level detection (ms)
    static constexpr float WINDOW = MS2S(20.0f);
    // Hold time after each attenuation burst (ms)
    static constexpr float HOLD = MS2S(80.0f);
    // Soft-knee half-width in dB (0 = hard)
    static constexpr float KNEE_DB = 6.0f;
    // Maximum attenuation (positive number, dB)
    static constexpr float MAX_ATTEN_DB = 24.0f;
    // Brick-wall peak limit (negative, dBFS)
    static constexpr float LIMITER_DB = -0.1f;

public:
    /**
     * @param targetDb Desired long-term RMS in dBFS  (e.g. -18).
     */
    AutoVolume();

    /** Reset envelopes, gain and delay-line (call on fs/parameter change). */
    void reset();

    /** In-place processing of a mono block of 32-bit floats in [-1,1]. */
    void process_block(int16_t* buffer, size_t samples);

    float get_target_db() { return _target_db; }
    void set_target_db(float target_db) {
        _target_db = target_db;
        reset();
    }
    float get_offset_db() { return _offset_db; }
    void set_offset_db(float offset_db) {
        _offset_db = offset_db;
        reset();
    }

private:
    /* constants */
    const float _limiter_linear;

    float _target_db = -18.0f;
    float _offset_db{};
    // RMS envelope (linear)
    float _envelope = 0.0f;
    // Applied attenuation dB
    float _gain_db = 0.0f;
    // Linear gain factor
    float _gain_linear = 1.0f;
    // Seconds left in hold
    float _hold_timer = 0.0f;
    // Look-ahead buffer
    std::vector<float> _delay_buffer;
    // Write pointer into the look-ahead buffer
    size_t _delay_offset = 0;
};
