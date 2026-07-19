#include "settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"

#define SETTINGS_NVS_NAMESPACE "settings"
#define SETTINGS_NVS_KEY       "configuration"

static const char *TAG = "settings";

static openaudio_settings_t current_settings;

static bool initialized = false;
static bool restart_required = false;

/*
 * Standardwerte, die deinem aktuell funktionierenden
 * Programm entsprechen.
 */
static const openaudio_settings_t default_settings = {
    .version = OPENAUDIO_SETTINGS_VERSION,

    .audio_buffer_kib = 128,
    .audio_block_frames = 256,
    .mp3_decode_threshold_bytes = 2048,
    .playback_prebuffer_ms = 250,
    .volume_fade_ms = 250,

    .http_buffer_bytes = 4096,
    .http_timeout_ms = 15000,
    .reconnect_delay_ms = 3000,
    .maximum_redirects = 5,

    .icy_metadata_enabled = false,
    .underrun_logging_enabled = true,
};

static bool valid_audio_buffer_size(
    uint32_t size_kib
)
{
    return size_kib == 32U ||
           size_kib == 64U ||
           size_kib == 128U ||
           size_kib == 256U;
}

static bool valid_audio_block_frames(
    uint32_t frame_count
)
{
    return frame_count == 128U ||
           frame_count == 256U ||
           frame_count == 512U;
}

static esp_err_t validate_settings(
    const openaudio_settings_t *settings
)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (settings->version !=
        OPENAUDIO_SETTINGS_VERSION) {

        ESP_LOGE(
            TAG,
            "Ungültige Einstellungs-Version: %u",
            (unsigned int)settings->version
        );

        return ESP_ERR_INVALID_VERSION;
    }

    if (!valid_audio_buffer_size(
            settings->audio_buffer_kib
        )) {

        ESP_LOGE(
            TAG,
            "Ungültige Audiopuffergröße: %u KiB",
            (unsigned int)settings->audio_buffer_kib
        );

        return ESP_ERR_INVALID_ARG;
    }

    if (!valid_audio_block_frames(
            settings->audio_block_frames
        )) {

        ESP_LOGE(
            TAG,
            "Ungültige Audioblockgröße: %u Frames",
            (unsigned int)settings->audio_block_frames
        );

        return ESP_ERR_INVALID_ARG;
    }

    if (settings->mp3_decode_threshold_bytes < 512U ||
        settings->mp3_decode_threshold_bytes > 8192U) {

        ESP_LOGE(
            TAG,
            "Ungültige MP3-Decodierschwelle: %u Byte",
            (unsigned int)
                settings->mp3_decode_threshold_bytes
        );

        return ESP_ERR_INVALID_ARG;
    }

    if (settings->playback_prebuffer_ms > 5000U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (settings->volume_fade_ms > 5000U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (settings->http_buffer_bytes < 1024U ||
        settings->http_buffer_bytes > 32768U) {

        return ESP_ERR_INVALID_ARG;
    }

    if (settings->http_timeout_ms < 1000U ||
        settings->http_timeout_ms > 120000U) {

        return ESP_ERR_INVALID_ARG;
    }

    if (settings->reconnect_delay_ms < 500U ||
        settings->reconnect_delay_ms > 60000U) {

        return ESP_ERR_INVALID_ARG;
    }

    if (settings->maximum_redirects > 10U) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static bool requires_restart(
    const openaudio_settings_t *old_settings,
    const openaudio_settings_t *new_settings
)
{
    if (old_settings == NULL ||
        new_settings == NULL) {

        return false;
    }

    /*
     * Diese Speicherbereiche werden derzeit nur
     * beim Start erzeugt.
     */
    return
        old_settings->audio_buffer_kib !=
            new_settings->audio_buffer_kib ||

        old_settings->audio_block_frames !=
            new_settings->audio_block_frames ||

        old_settings->http_buffer_bytes !=
            new_settings->http_buffer_bytes;
}

static esp_err_t load_from_nvs(void)
{
    nvs_handle_t handle;

    esp_err_t result = nvs_open(
        SETTINGS_NVS_NAMESPACE,
        NVS_READONLY,
        &handle
    );

    if (result != ESP_OK) {
        return result;
    }

    size_t stored_size =
        sizeof(current_settings);

    result = nvs_get_blob(
        handle,
        SETTINGS_NVS_KEY,
        &current_settings,
        &stored_size
    );

    nvs_close(handle);

    if (result != ESP_OK) {
        return result;
    }

    if (stored_size !=
        sizeof(current_settings)) {

        ESP_LOGW(
            TAG,
            "Gespeicherte Einstellungsstruktur hat falsche Größe"
        );

        return ESP_ERR_INVALID_SIZE;
    }

    return validate_settings(
        &current_settings
    );
}

esp_err_t settings_save(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(
        validate_settings(
            &current_settings
        ),
        TAG,
        "Einstellungen sind ungültig"
    );

    nvs_handle_t handle;

    esp_err_t result = nvs_open(
        SETTINGS_NVS_NAMESPACE,
        NVS_READWRITE,
        &handle
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "NVS konnte nicht geöffnet werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result = nvs_set_blob(
        handle,
        SETTINGS_NVS_KEY,
        &current_settings,
        sizeof(current_settings)
    );

    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }

    nvs_close(handle);

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Einstellungen konnten nicht gespeichert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "Einstellungen im NVS gespeichert"
    );

    return ESP_OK;
}

esp_err_t settings_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    memset(
        &current_settings,
        0,
        sizeof(current_settings)
    );

    esp_err_t result =
        load_from_nvs();

    if (result == ESP_OK) {
        initialized = true;

        ESP_LOGI(
            TAG,
            "Einstellungen aus NVS geladen"
        );

        ESP_LOGI(
            TAG,
            "Audiopuffer: %u KiB",
            (unsigned int)
                current_settings.audio_buffer_kib
        );

        ESP_LOGI(
            TAG,
            "MP3-Decodierschwelle: %u Byte",
            (unsigned int)
                current_settings
                    .mp3_decode_threshold_bytes
        );

        return ESP_OK;
    }

    ESP_LOGW(
        TAG,
        "Keine gültigen Einstellungen gefunden: %s",
        esp_err_to_name(result)
    );

    current_settings =
        default_settings;

    initialized = true;

    result = settings_save();

    if (result != ESP_OK) {
        initialized = false;
        return result;
    }

    ESP_LOGI(
        TAG,
        "Standardeinstellungen angelegt"
    );

    return ESP_OK;
}

esp_err_t settings_get(
    openaudio_settings_t *output
)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (output == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *output =
        current_settings;

    return ESP_OK;
}

esp_err_t settings_update(
    const openaudio_settings_t *new_settings
)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(
        validate_settings(
            new_settings
        ),
        TAG,
        "Neue Einstellungen sind ungültig"
    );

    const openaudio_settings_t old_settings =
        current_settings;

    if (requires_restart(
            &old_settings,
            new_settings
        )) {

        restart_required = true;
    }

    current_settings =
        *new_settings;

    esp_err_t result =
        settings_save();

    if (result != ESP_OK) {
        current_settings =
            old_settings;

        return result;
    }

    ESP_LOGI(
        TAG,
        "Einstellungen aktualisiert"
    );

    return ESP_OK;
}

esp_err_t settings_reset(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const openaudio_settings_t old_settings =
        current_settings;

    if (requires_restart(
            &old_settings,
            &default_settings
        )) {

        restart_required = true;
    }

    current_settings =
        default_settings;

    esp_err_t result =
        settings_save();

    if (result != ESP_OK) {
        current_settings =
            old_settings;

        return result;
    }

    ESP_LOGI(
        TAG,
        "Standardeinstellungen wiederhergestellt"
    );

    return ESP_OK;
}

bool settings_restart_required(void)
{
    return restart_required;
}

void settings_clear_restart_required(void)
{
    restart_required = false;
}