#ifndef OPENAUDIO_AUDIO_DSP_H
#define OPENAUDIO_AUDIO_DSP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_DSP_CHANNELS 2U

#define AUDIO_DSP_MIN_GAIN_DB (-60.0f)
#define AUDIO_DSP_MAX_GAIN_DB (12.0f)

#define AUDIO_DSP_MIN_TONE_DB (-12.0f)
#define AUDIO_DSP_MAX_TONE_DB (12.0f)

#define AUDIO_DSP_MIN_BALANCE (-1.0f)
#define AUDIO_DSP_MAX_BALANCE (1.0f)
#define AUDIO_DSP_EQ_BANDS 10

/*
 * Zentrale DSP-Einstellungen.
 *
 * balance:
 *   -1.0 = vollständig links
 *    0.0 = Mitte
 *   +1.0 = vollständig rechts
 *
 * gain_db:
 *   Zusätzliche digitale Verstärkung beziehungsweise Absenkung.
 *
 * bass_db / treble_db:
 *   Werden bereits gespeichert, die Filter werden im nächsten Schritt
 *   in die Verarbeitung eingebaut.
 */
 typedef struct

{
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
    float z1L;
    float z2L;
    float z1R;
    float z2R;
} audio_biquad_t;
typedef struct {
    bool enabled;
    bool muted;

    float gain_db;
    float balance;

    float bass_db;
    float treble_db;

    float eq[AUDIO_DSP_EQ_BANDS];
    bool loudness_enabled;
    bool limiter_enabled;

    float limiter_threshold_db;
} audio_dsp_settings_t;

/*
 * DSP initialisieren.
 *
 * sample_rate:
 *   Aktuelle PCM-Samplerate, beispielsweise 44100 oder 48000 Hz.
 *
 * channels:
 *   OpenAudio32 verarbeitet aktuell Stereo, also 2 Kanäle.
 */
esp_err_t audio_dsp_init(
    uint32_t sample_rate,
    uint8_t channels
);

/*
 * DSP zurücksetzen und Ressourcen freigeben.
 */
void audio_dsp_deinit(void);

/*
 * Samplerate ändern.
 *
 * Wird benötigt, wenn der Stream von 44,1 auf 48 kHz wechselt.
 */
esp_err_t audio_dsp_set_sample_rate(
    uint32_t sample_rate
);

uint32_t audio_dsp_get_sample_rate(void);

/*
 * Gesamte DSP-Konfiguration setzen beziehungsweise auslesen.
 */
esp_err_t audio_dsp_set_settings(
    const audio_dsp_settings_t *settings
);

esp_err_t audio_dsp_get_settings(
    audio_dsp_settings_t *settings
);

/*
 * Standardwerte laden.
 */
esp_err_t audio_dsp_reset_settings(void);

/*
 * Einzelne häufig benötigte Werte.
 */
esp_err_t audio_dsp_set_enabled(bool enabled);

bool audio_dsp_is_enabled(void);

esp_err_t audio_dsp_set_mute(bool muted);

bool audio_dsp_is_muted(void);

esp_err_t audio_dsp_set_gain_db(float gain_db);

float audio_dsp_get_gain_db(void);

esp_err_t audio_dsp_set_balance(float balance);

float audio_dsp_get_balance(void);

esp_err_t audio_dsp_set_bass_db(float bass_db);

float audio_dsp_get_bass_db(void);

esp_err_t audio_dsp_set_treble_db(float treble_db);

float audio_dsp_get_treble_db(void);

/*
 * Interleaved Stereo-PCM direkt im vorhandenen Puffer bearbeiten.
 *
 * Beispiel:
 *
 * samples:
 *   L0, R0, L1, R1, L2, R2 ...
 *
 * frame_count:
 *   Anzahl Stereo-Frames, nicht Anzahl einzelner int16_t-Werte.
 */
esp_err_t audio_dsp_process_s16(
    int16_t *samples,
    size_t frame_count
);
esp_err_t audio_dsp_set_eq_band(
    size_t band_index,
    float gain_db
);

#ifdef __cplusplus
}
#endif

#endif