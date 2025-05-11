
#include "support.h"

#include "AutoVolume.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

LOG_TAG(AutoVolume);

#define MONITOR_AUTO_VOLUME 0

AutoVolume::AutoVolume() : _limiter_linear(pow(10.0f, LIMITER_DB / 20.0f)) {
    const size_t lookahead_samples = (size_t)US_TO_SAMPLES(LOOKAHEAD_MS * 1000.0f);

    _delay_buffer.assign(lookahead_samples, 0.0f);

    reset();

#if MONITOR_AUTO_VOLUME
    FREERTOS_CHECK(xTaskCreate(
        [](void *param) {
            while (true) {
                const auto self = (AutoVolume *)param;

                ESP_LOGI(TAG, "Envelope %f gain db %f gain linear %f hold timer %f", self->_envelope, self->_gain_db,
                         self->_gain_linear, self->_hold_timer);

                vTaskDelay(pdMS_TO_TICKS(500));
            }
        },
        "auto_volume_monitor", CONFIG_ESP_MAIN_TASK_STACK_SIZE, this, 5, nullptr));
#endif
}

void AutoVolume::reset() {
    _envelope = 0.0f;
    _gain_db = 0.0f;
    _gain_linear = 1.0f;
    _hold_timer = 0.0f;

    fill(_delay_buffer.begin(), _delay_buffer.end(), 0.0f);
    _delay_offset = 0;
}

void AutoVolume::process_block(int16_t *buffer, size_t samples) {
    if (samples == 0) {
        return;
    }

    /*---------------- 1. Block RMS (for detector) ----------------*/
    float sum_sq = 0.0f;
    for (size_t i = 0; i < samples; ++i) {
        const float sample = (float)buffer[i] / INT16_MAX;
        sum_sq += sample * sample;
    }
    const float rms = sqrt(sum_sq / (float)samples);

    /*---------------- 2. Envelope (one-pole, block-rate) ---------*/
    const float block_sec = (float)samples / CONFIG_DEVICE_I2S_SAMPLE_RATE;
    const float alpha_env = expf(-block_sec / WINDOW);
    _envelope = alpha_env * _envelope + (1.0f - alpha_env) * rms;

    /*---------------- 3. Desired attenuation (soft knee) ---------*/
    const float env_db = 20.0f * log10f(_envelope + numeric_limits<float>::min());  // denormal-safe
    const float diff_db = env_db - _target_db;                                      // + = loud
    float err_db = 0.0f;                                                            // ≤ 0

    if (diff_db > 0.0f) {  // only attenuate
        if (KNEE_DB <= 0.0f || diff_db >= KNEE_DB * 0.5f) {
            err_db = -diff_db;                         // hard region
        } else {                                       // quadratic knee  |diff| < knee/2
            const float x = diff_db + KNEE_DB * 0.5f;  // 0…knee
            err_db = -(x * x) / (2.0f * KNEE_DB);
        }
    }

    /*---------------- 4. Attack / hold / release logic -----------*/
    const bool need_more_attenuation = (err_db < _gain_db);  // more neg
    if (need_more_attenuation) {
        _hold_timer = HOLD;  // reset hold
    } else if (_hold_timer > 0.0f) {
        _hold_timer = max(0.0f, _hold_timer - block_sec);  // count down
    }

    const float tc = need_more_attenuation ? ATTACK : (_hold_timer > 0.0f ? 1e9f /* freeze */ : RELEASE);

    const float alpha_g = expf(-block_sec / tc);
    _gain_db = alpha_g * _gain_db + (1.0f - alpha_g) * err_db;

    /* Clamp: never amplify, never exceed max attenuation */
    _gain_db = clamp(_gain_db, -MAX_ATTEN_DB, 0.0f);
    _gain_linear = pow(10.0f, _gain_db / 20.0f);

    float output_gain_db = _gain_db + _offset_db;
    float output_gain_linear = pow(10.0f, output_gain_db / 20.0f);

    /*---------------- 5. Apply gain + look-ahead + limiter -------*/
    for (size_t i = 0; i < samples; ++i) {
        float dry = (float)buffer[i] / INT16_MAX;

        /* circular delay line */
        float delayed = _delay_buffer[_delay_offset];
        _delay_buffer[_delay_offset] = dry;
        _delay_offset = (_delay_offset + 1) % _delay_buffer.size();

        float wet = delayed * output_gain_linear;

        /* peak limiter (hard clip) */
        if (wet > _limiter_linear)
            wet = _limiter_linear;
        if (wet < -_limiter_linear)
            wet = -_limiter_linear;

        buffer[i] = (int16_t)(wet * INT16_MAX);
    }
}
