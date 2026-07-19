#pragma once

#include "esp_err.h"
#include <stdbool.h>
/**
 * Streamt eine unkomprimierte WAV-Datei direkt per HTTP.
 *
 * Unterstützt:
 * - PCM
 * - 16 Bit
 * - Stereo
 * - 44.100 Hz
 */
esp_err_t streamer_play_wav_stream(const char *url);
esp_err_t streamer_play_mp3_stream(const char *url);
esp_err_t streamer_play_mp3_radio(const char *url);
void streamer_radio_request_stop(void);

void streamer_radio_clear_stop(void);

bool streamer_radio_stop_requested(void);