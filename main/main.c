

#include "audio.h"
#include "wifi.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "web_server.h"

#include "station_manager.h"
#include "radio_player.h"
#include "settings.h"

#include "spotify_player.h"

static const char *TAG = "OpenAudio32";

#define WIFI_SSID     "AEGT"
#define WIFI_PASSWORD "XXX"

void app_main(void)
{
    ESP_LOGI(TAG, "OpenAudio32 startet");

    esp_err_t nvs_result =
        nvs_flash_init();

    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(
            nvs_flash_erase()
        );

        nvs_result =
            nvs_flash_init();
    }

    ESP_ERROR_CHECK(
        nvs_result
    );

    ESP_ERROR_CHECK(
    settings_init()
    );
    ESP_ERROR_CHECK(
    spotify_player_init()
    );
    ESP_ERROR_CHECK(
        station_manager_init()
    );

    radio_station_t current_station = {0};

    ESP_ERROR_CHECK(
        station_manager_get_current(
            &current_station
        )
    );

    ESP_LOGI(
        TAG,
        "Gespeicherter Sender: %s",
        current_station.name
    );

    ESP_LOGI(
        TAG,
        "Stream-URL: %s",
        current_station.url
    );

    ESP_ERROR_CHECK(
        wifi_init_sta(
            WIFI_SSID,
            WIFI_PASSWORD
        )
    );

    ESP_LOGI(
        TAG,
        "WLAN-Verbindung erfolgreich"
    );

    ESP_ERROR_CHECK(
        web_server_start()
    );

    ESP_LOGI(
        TAG,
        "Webinterface gestartet"
    );
ESP_ERROR_CHECK(
    spotify_player_start()
);

ESP_LOGI(
    TAG,
    "Spotify Connect gestartet"
);
    ESP_ERROR_CHECK(
        audio_init()
    );

    ESP_ERROR_CHECK(
        audio_start()
    );

    audio_set_volume(0.10f);
    audio_set_mute(false);

    ESP_ERROR_CHECK(
        radio_player_init()
    );

    ESP_ERROR_CHECK(
        radio_player_play_current()
    );

    ESP_LOGI(
        TAG,
        "Gespeicherter Radiosender gestartet"
    );
}