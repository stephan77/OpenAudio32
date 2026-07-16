#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

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

void audio_set_volume(float volume);
float audio_get_volume(void);

void audio_set_mute(bool mute);
bool audio_is_muted(void);

esp_err_t audio_write_silence(uint32_t duration_ms);