#include "airplay_core.h"

#include <stdbool.h>

#include "airplay_pcm_task.h"
#include "audio_output.h"
#include "audio_receiver.h"
#include "mdns_airplay.h"

#include "rtsp_server.h"
#include "dacp_client.h"
#include "mdns_airplay.h"

#include "rtsp_server.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG =
    "airplay_core";

static bool initialized =
    false;

static bool started =
    false;

esp_err_t airplay_core_init(void)
{
    if (initialized) {
        return ESP_OK;
    }
dacp_init();
    esp_err_t result =
        audio_output_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Audio-Adapter konnte nicht initialisiert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        audio_receiver_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "AirPlay-Receiver konnte nicht initialisiert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    initialized = true;

    ESP_LOGI(
        TAG,
        "AirPlay-Core initialisiert"
    );

    return ESP_OK;
}

esp_err_t airplay_core_start(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (started) {
        return ESP_OK;
    }

    esp_err_t result =
        rtsp_server_start();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "RTSP-Server konnte nicht gestartet werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    mdns_airplay_init();

    /*
     * audio_output_start() liefert void.
     * Deshalb darf der Rückgabewert nicht result zugewiesen werden.
     */
    audio_output_start();

    /*
     * Dieser Task liest decodierte AirPlay-PCM-Daten aus
     * audio_receiver_read() und übergibt sie mit audio_submit()
     * an die OpenAudio32-Audioengine.
     */
    result =
        airplay_pcm_task_start();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "AirPlay-PCM-Task konnte nicht gestartet werden: %s",
            esp_err_to_name(result)
        );

        rtsp_server_stop();
        audio_output_stop();

        return result;
    }

    started = true;

    ESP_LOGI(
        TAG,
        "AirPlay-Core gestartet"
    );

    return ESP_OK;
}

esp_err_t airplay_core_stop(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!started) {
        return ESP_OK;
    }

    const esp_err_t result =
        airplay_pcm_task_stop();

    if (result != ESP_OK) {
        return result;
    }

    audio_output_stop();

    started =
        false;

    ESP_LOGI(
        TAG,
        "AirPlay-Core gestoppt"
    );

    return ESP_OK;
}