#include "audio.h"
#include "wifi_manager.h"
#include "airplay_player.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "web_server.h"

#include "station_manager.h"
#include "radio_player.h"
#include "settings.h"
#include "bt_receiver.h"

#include "esp_heap_caps.h"
#include "media_manager.h"

static const char *TAG = "OpenAudio32";

void app_main(void)
{
    ESP_LOGI(TAG, "OpenAudio32 startet");

    ESP_LOGI(TAG, "INTERN frei: %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    ESP_LOGI(TAG, "PSRAM frei: %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(settings_init());
    ESP_ERROR_CHECK(media_manager_init());

    /*
     * Audio-Hardware und gemeinsame Audiopipeline zuerst starten.
     */
    ESP_ERROR_CHECK(audio_init());
    ESP_ERROR_CHECK(audio_start());

    audio_set_volume(0.10f);
    audio_set_mute(false);

    /*
     * Netzwerk starten.
     */
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(wifi_manager_start());

    ESP_ERROR_CHECK(station_manager_init());
    ESP_ERROR_CHECK(radio_player_init());

    /*
     * AirPlay erst initialisieren, wenn Audio und Netzwerk vorhanden sind.
     */
    esp_err_t airplay_result =
    airplay_player_init();

if (airplay_result != ESP_OK) {
    ESP_LOGE(
        TAG,
        "AirPlay konnte nicht gestartet werden: %s",
        esp_err_to_name(airplay_result)
    );
}
    ESP_ERROR_CHECK(airplay_player_start());

    ESP_LOGI(TAG, "AirPlay-Grunddienst gestartet");
esp_err_t bt_receiver_result =
    bt_receiver_init();

if (bt_receiver_result != ESP_OK) {
    ESP_LOGE(
        TAG,
        "Bluetooth-UART-Empfänger konnte nicht gestartet werden: %s",
        esp_err_to_name(bt_receiver_result)
    );
} else {
    ESP_LOGI(
        TAG,
        "Bluetooth-UART-Empfänger gestartet"
    );
}
    ESP_ERROR_CHECK(web_server_start());
    ESP_LOGI(TAG, "Webinterface gestartet");

    esp_err_t radio_result = radio_player_play_current();

    if (radio_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Gespeicherter Radiosender konnte noch nicht gestartet werden: %s",
            esp_err_to_name(radio_result)
        );
    }
}