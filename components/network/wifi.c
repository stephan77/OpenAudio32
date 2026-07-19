#include "wifi.h"

#include <stdbool.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT    BIT1

#define WIFI_MAX_RETRIES   10

static const char *TAG = "wifi";

static EventGroupHandle_t wifi_event_group = NULL;
static esp_netif_t *station_netif = NULL;

static int retry_count = 0;
static bool wifi_initialized = false;

static void wifi_event_handler(
    void *handler_argument,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {

        ESP_LOGI(TAG, "WLAN-Station gestartet");
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED) {

        const wifi_event_sta_disconnected_t *disconnected =
            (const wifi_event_sta_disconnected_t *)event_data;

        ESP_LOGW(
            TAG,
            "WLAN getrennt, Grundcode: %d",
            disconnected != NULL ? disconnected->reason : -1
        );

        if (retry_count < WIFI_MAX_RETRIES) {
            retry_count++;

            ESP_LOGI(
                TAG,
                "Neuer Verbindungsversuch %d von %d",
                retry_count,
                WIFI_MAX_RETRIES
            );

            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "WLAN-Verbindung endgültig fehlgeschlagen");

            xEventGroupSetBits(
                wifi_event_group,
                WIFI_FAILED_BIT
            );
        }

        return;
    }

    if (event_base == IP_EVENT &&
        event_id == IP_EVENT_STA_GOT_IP) {

        const ip_event_got_ip_t *event =
            (const ip_event_got_ip_t *)event_data;

        retry_count = 0;

        ESP_LOGI(
            TAG,
            "IP-Adresse: " IPSTR,
            IP2STR(&event->ip_info.ip)
        );

        ESP_LOGI(
            TAG,
            "Gateway: " IPSTR,
            IP2STR(&event->ip_info.gw)
        );

        ESP_LOGI(
            TAG,
            "Netzmaske: " IPSTR,
            IP2STR(&event->ip_info.netmask)
        );

        xEventGroupSetBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}

esp_err_t wifi_init_sta(
    const char *ssid,
    const char *password
)
{
    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(ssid) == 0) {
        ESP_LOGE(TAG, "SSID darf nicht leer sein");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(ssid) > 32) {
        ESP_LOGE(TAG, "SSID ist zu lang");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(password) > 64) {
        ESP_LOGE(TAG, "WLAN-Passwort ist zu lang");
        return ESP_ERR_INVALID_ARG;
    }

    if (wifi_initialized) {
        ESP_LOGW(TAG, "WLAN wurde bereits initialisiert");
        return ESP_OK;
    }

    wifi_event_group = xEventGroupCreate();

    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG, "WLAN-Eventgruppe konnte nicht erstellt werden");
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(
        esp_netif_init(),
        TAG,
        "esp_netif_init fehlgeschlagen"
    );

    esp_err_t event_loop_result =
        esp_event_loop_create_default();

    if (event_loop_result != ESP_OK &&
        event_loop_result != ESP_ERR_INVALID_STATE) {

        ESP_LOGE(
            TAG,
            "Default Event Loop konnte nicht erstellt werden: %s",
            esp_err_to_name(event_loop_result)
        );

        return event_loop_result;
    }

    station_netif = esp_netif_create_default_wifi_sta();

    if (station_netif == NULL) {
        ESP_LOGE(TAG, "WLAN-Netzwerkinterface konnte nicht erstellt werden");
        return ESP_FAIL;
    }

    wifi_init_config_t init_config =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_RETURN_ON_ERROR(
        esp_wifi_init(&init_config),
        TAG,
        "esp_wifi_init fehlgeschlagen"
    );

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL
        ),
        TAG,
        "WLAN-Eventhandler konnte nicht registriert werden"
    );

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL
        ),
        TAG,
        "IP-Eventhandler konnte nicht registriert werden"
    );

    wifi_config_t wifi_config = {0};

    strncpy(
        (char *)wifi_config.sta.ssid,
        ssid,
        sizeof(wifi_config.sta.ssid) - 1
    );

    strncpy(
        (char *)wifi_config.sta.password,
        password,
        sizeof(wifi_config.sta.password) - 1
    );

    wifi_config.sta.threshold.authmode =
        WIFI_AUTH_WPA2_PSK;

    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(
        esp_wifi_set_mode(WIFI_MODE_STA),
        TAG,
        "Station-Modus konnte nicht gesetzt werden"
    );

    ESP_RETURN_ON_ERROR(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        ),
        TAG,
        "WLAN-Konfiguration konnte nicht gesetzt werden"
    );

    ESP_RETURN_ON_ERROR(
        esp_wifi_start(),
        TAG,
        "WLAN konnte nicht gestartet werden"
    );
    ESP_RETURN_ON_ERROR(

    esp_wifi_set_ps(WIFI_PS_NONE),

    TAG,

    "WLAN-Powersave konnte nicht deaktiviert werden"

);

    wifi_initialized = true;

    ESP_LOGI(TAG, "Verbinde mit SSID: %s", ssid);

    EventBits_t result_bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    if ((result_bits & WIFI_CONNECTED_BIT) != 0) {
        wifi_ap_record_t access_point = {0};

        esp_err_t ap_result =
            esp_wifi_sta_get_ap_info(&access_point);

        if (ap_result == ESP_OK) {
            ESP_LOGI(
                TAG,
                "Verbunden mit %s, RSSI: %d dBm, Kanal: %d",
                access_point.ssid,
                access_point.rssi,
                access_point.primary
            );
        }

        return ESP_OK;
    }

    ESP_LOGE(TAG, "Verbindung mit %s fehlgeschlagen", ssid);
    return ESP_FAIL;
}