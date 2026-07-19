#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OPENAUDIO_SETTINGS_VERSION 1U

/*
 * Gespeicherte Einstellungen von OpenAudio32.
 *
 * Achtung:
 * Änderungen an dieser Struktur erfordern später
 * eine Erhöhung von OPENAUDIO_SETTINGS_VERSION.
 */
typedef struct {
    uint32_t version;

    /*
     * Audio
     */
    uint32_t audio_buffer_kib;
    uint32_t audio_block_frames;
    uint32_t mp3_decode_threshold_bytes;
    uint32_t playback_prebuffer_ms;
    uint32_t volume_fade_ms;

    /*
     * Netzwerk und Streaming
     */
    uint32_t http_buffer_bytes;
    uint32_t http_timeout_ms;
    uint32_t reconnect_delay_ms;
    uint32_t maximum_redirects;

    /*
     * Funktionen
     */
    bool icy_metadata_enabled;
    bool underrun_logging_enabled;
} openaudio_settings_t;

/**
 * Lädt die Einstellungen aus NVS.
 *
 * Sind keine gültigen Einstellungen vorhanden,
 * werden die Standardwerte gespeichert.
 */
esp_err_t settings_init(void);

/**
 * Kopiert die aktuellen Einstellungen nach output.
 */
esp_err_t settings_get(
    openaudio_settings_t *output
);

/**
 * Ersetzt und speichert alle Einstellungen.
 */
esp_err_t settings_update(
    const openaudio_settings_t *new_settings
);

/**
 * Stellt die Standardwerte wieder her.
 */
esp_err_t settings_reset(void);

/**
 * Speichert die aktuell im RAM liegenden Einstellungen.
 */
esp_err_t settings_save(void);

/**
 * Liefert true, wenn eine geänderte Einstellung
 * einen Neustart benötigt.
 */
bool settings_restart_required(void);

/**
 * Löscht den Neustart-Hinweis.
 */
void settings_clear_restart_required(void);

#ifdef __cplusplus
}
#endif