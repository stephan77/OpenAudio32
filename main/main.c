//#define WIFI_SSID     "AEGT"
//#define WIFI_PASSWORD "icharbeitebeifordinsaarlouisseit1992"

#include "audio.h"
#include "streamer.h"
#include "wifi.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "OpenAudio32";

#define WIFI_SSID     "AEGT"
#define WIFI_PASSWORD "icharbeitebeifordinsaarlouisseit1992"

#define WAV_TEST_URL \
    "http://192.168.179.119:8000/test.wav"

void app_main(void)
{
    ESP_LOGI(TAG, "OpenAudio32 startet");

    esp_err_t nvs_result = nvs_flash_init();

    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }

    ESP_ERROR_CHECK(nvs_result);

    ESP_ERROR_CHECK(
        wifi_init_sta(
            WIFI_SSID,
            WIFI_PASSWORD
        )
    );

    ESP_LOGI(TAG, "WLAN-Verbindung erfolgreich");

    ESP_ERROR_CHECK(audio_init());
    ESP_ERROR_CHECK(audio_start());

    audio_set_volume(0.10f);
    audio_set_mute(false);

    ESP_LOGI(TAG, "Starte Netzwerk-WAV-Test");

    esp_err_t stream_result =
        streamer_play_wav_stream(WAV_TEST_URL);

    if (stream_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Netzwerk-WAV fehlgeschlagen: %s",
            esp_err_to_name(stream_result)
        );

        return;
    }

    ESP_LOGI(TAG, "Netzwerk-WAV erfolgreich abgespielt");
}