#pragma once

#include "esp_err.h"

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