#include "bluetooth.h"
#include "bt_link.h"

#include "esp_err.h"
#include "esp_log.h"
#include "bt_audio_output.h"
#include "nvs_flash.h"

static const char *TAG = "OpenAudio32_BT";

void app_main(void)
{
    ESP_LOGI(TAG, "==================================");
    ESP_LOGI(TAG, " OpenAudio32 Bluetooth Firmware");
    ESP_LOGI(TAG, " ESP32-WROOM gestartet");
    ESP_LOGI(TAG, "==================================");

    esp_err_t result = nvs_flash_init();

    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }

    ESP_ERROR_CHECK(result);

    ESP_LOGI(TAG, "NVS erfolgreich initialisiert");

    result = bt_link_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "UART-Verbindung konnte nicht gestartet werden: %s",
            esp_err_to_name(result)
        );

        return;
    }

    bt_link_send("BT_BOOT");
 result =
    bt_audio_output_init();

if (result != ESP_OK) {
    ESP_LOGE(
        TAG,
        "Bluetooth-I2S konnte nicht gestartet werden: %s",
        esp_err_to_name(result)
    );

    return;
}

    result = bluetooth_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bluetooth konnte nicht gestartet werden: %s",
            esp_err_to_name(result)
        );

        bt_link_send("BT_ERROR");
        return;
    }

    ESP_LOGI(TAG, "Bluetooth erfolgreich gestartet");

    bt_link_send("BT_READY");
}