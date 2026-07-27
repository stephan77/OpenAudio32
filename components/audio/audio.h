#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define AUDIO_SAMPLE_RATE_HZ 44100
#define AUDIO_CHANNEL_COUNT  2

esp_err_t audio_init(void);

/**
 * Startet den eigenständigen Audio-Ausgabe-Task.
 */
esp_err_t audio_start(void);

/**
 * Übergibt Stereo-PCM-Daten an den Audiopuffer.
 *
 * Format:
 * - 16 Bit signed little-endian
 * - 44,1 kHz
 * - Stereo interleaved: L, R, L, R ...
 */
esp_err_t audio_submit(
    const int16_t *samples,
    size_t frame_count,
    uint32_t timeout_ms
);

/**
 * Leert noch gepufferte Audiodaten.
 */
esp_err_t audio_flush(uint32_t timeout_ms);
esp_err_t audio_clear_buffer(void);

size_t audio_get_buffered_bytes(void);
void audio_set_playback_active(bool active);

void audio_set_volume(float volume);
float audio_get_volume(void);

void audio_set_mute(bool mute);
bool audio_is_muted(void);

esp_err_t audio_write_silence(uint32_t duration_ms);
uint32_t audio_get_underrun_count(void);

bool audio_is_playback_active(void);
esp_err_t audio_set_sample_rate(uint32_t sample_rate);

uint32_t audio_get_sample_rate(void);

#define AUDIO_EQUALIZER_BAND_COUNT 10

typedef struct {
    bool equalizer_enabled;

    /*
     * Frequenzbänder:
     * 31, 62, 125, 250, 500,
     * 1000, 2000, 4000, 8000, 16000 Hz
     *
     * Wertebereich: -12,0 bis +12,0 dB
     */
    float equalizer_bands_db[
        AUDIO_EQUALIZER_BAND_COUNT
    ];

    float bass_db;
    float treble_db;
    float balance;

    bool loudness_enabled;
    bool limiter_enabled;
    float limiter_threshold_db;

    uint8_t startup_volume;
    uint8_t maximum_volume;
} audio_settings_t;

esp_err_t audio_get_settings(
    audio_settings_t *settings
);

esp_err_t audio_set_settings(
    const audio_settings_t *settings
);

esp_err_t audio_reset_settings(void);
#ifdef __cplusplus
}
#endif