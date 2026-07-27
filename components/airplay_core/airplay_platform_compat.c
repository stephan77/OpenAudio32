#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "audio.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_log.h"

static const char *TAG = "airplay_compat";

/*
 * Diese Funktionen erwartet der importierte AirPlay-Core aus seinem
 * ursprünglichen Projekt. OpenAudio32 hat jedoch ein eigenes Settings-
 * und WiFi-System. Daher stellen wir hier die benötigte Schnittstelle bereit.
 */

static float s_airplay_volume_db = -20.0f;

esp_err_t settings_get_device_name(char *name, size_t len)
{
    if (name == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(name, len, "%s", "OpenAudio32");
    return ESP_OK;
}

esp_err_t settings_get_volume(float *volume_db)
{
    if (volume_db == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *volume_db = s_airplay_volume_db;
    return ESP_OK;
}

esp_err_t settings_set_volume(float volume_db)
{
    s_airplay_volume_db = volume_db;

    /*
     * AirPlay verwendet normalerweise:
     *
     *   0 dB    = maximale Lautstärke
     *  -30 dB   = minimale Lautstärke / stumm
     *
     * Auf den OpenAudio32-Bereich 0.0 bis 1.0 abbilden.
     */
    float volume;

    if (volume_db <= -30.0f) {
        volume = 0.0f;
    } else if (volume_db >= 0.0f) {
        volume = 1.0f;
    } else {
        volume = (volume_db + 30.0f) / 30.0f;
    }

    audio_set_volume(volume);
    audio_set_mute(volume <= 0.0f);

    ESP_LOGI(
        TAG,
        "AirPlay-Lautstärke: %.2f dB -> %.1f %%",
        volume_db,
        volume * 100.0f
    );

    return ESP_OK;
}

esp_err_t settings_persist_volume(void)
{
    /*
     * Vorerst nur Kompatibilität.
     * Die dauerhafte Übergabe an das OpenAudio32-NVS kann später ergänzt werden.
     */
    return ESP_OK;
}

void wifi_get_mac_str(char *mac_str, size_t len)
{
    if (mac_str == NULL || len == 0) {
        return;
    }

    uint8_t mac[6] = {0};

    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WLAN-MAC konnte nicht gelesen werden: %s",
                 esp_err_to_name(err));
        snprintf(mac_str, len, "%s", "000000000000");
        return;
    }

    /*
     * AirPlay/RAOP verwendet die MAC ohne Doppelpunkte als Service-ID.
     */
    snprintf(mac_str, len,
             "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
}
