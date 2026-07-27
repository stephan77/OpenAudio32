#include "audio.h"
#include "wifi_manager.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "web_server.h"

#include "station_manager.h"
#include "radio_player.h"
#include "settings.h"
#include "spotify_player.h"
#include "esp_heap_caps.h"

static const char *TAG = "OpenAudio32";

void app_main(void)
{
    ESP_LOGI(TAG, "OpenAudio32 startet");
    ESP_LOGI(TAG,

        "INTERN frei : %u",

        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    ESP_LOGI(TAG,

        "PSRAM frei  : %u",

        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    esp_err_t nvs_result = nvs_flash_init();

    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }

    ESP_ERROR_CHECK(nvs_result);
    ESP_ERROR_CHECK(settings_init());
    ESP_ERROR_CHECK(wifi_manager_init());

    /*
     * Startet STA + Setup-AP. Der Start blockiert nicht dauerhaft,
     * damit das Webinterface auch ohne Heim-WLAN erreichbar bleibt.
     */
    ESP_ERROR_CHECK(wifi_manager_start());

    ESP_ERROR_CHECK(station_manager_init());
    ESP_ERROR_CHECK(spotify_player_init());

    ESP_ERROR_CHECK(web_server_start());
    ESP_LOGI(TAG, "Webinterface gestartet");

    ESP_ERROR_CHECK(spotify_player_start());
    ESP_LOGI(TAG, "Spotify Connect gestartet");

    ESP_ERROR_CHECK(audio_init());
    ESP_ERROR_CHECK(audio_start());

    audio_set_volume(0.10f);
    audio_set_mute(false);

    ESP_ERROR_CHECK(radio_player_init());

    esp_err_t radio_result = radio_player_play_current();
    if (radio_result != ESP_OK) {
        ESP_LOGW(TAG, "Gespeicherter Radiosender konnte noch nicht gestartet werden: %s",
                 esp_err_to_name(radio_result));
    }
}
