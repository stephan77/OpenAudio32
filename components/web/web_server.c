#include "web_server.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "audio_dsp.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "wifi_manager.h"
#include "esp_system.h"
#include "station_manager.h"
#include "radio_player.h"
#include "settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

extern const uint8_t js_wifi_start[] asm("_binary_wifi_js_start");
extern const uint8_t js_wifi_end[]   asm("_binary_wifi_js_end");
extern const uint8_t css_wifi_start[]
    asm("_binary_wifi_css_start");

extern const uint8_t css_wifi_end[]
    asm("_binary_wifi_css_end");
extern const uint8_t css_app_start[]
    asm("_binary_app_css_start");

extern const uint8_t css_app_end[]
    asm("_binary_app_css_end");

extern const uint8_t css_player_start[]
    asm("_binary_player_css_start");

extern const uint8_t css_player_end[]
    asm("_binary_player_css_end");

extern const uint8_t css_stations_start[]
    asm("_binary_stations_css_start");

extern const uint8_t css_stations_end[]
    asm("_binary_stations_css_end");

extern const uint8_t js_api_start[]
    asm("_binary_api_js_start");

extern const uint8_t js_api_end[]
    asm("_binary_api_js_end");

extern const uint8_t js_player_start[]
    asm("_binary_player_js_start");

extern const uint8_t js_player_end[]
    asm("_binary_player_js_end");

extern const uint8_t js_stations_start[]
    asm("_binary_stations_js_start");

extern const uint8_t js_stations_end[]
    asm("_binary_stations_js_end");

extern const uint8_t js_app_start[]
    asm("_binary_app_js_start");

extern const uint8_t js_app_end[]
    asm("_binary_app_js_end");

extern const uint8_t css_system_start[]

    asm("_binary_system_css_start");

extern const uint8_t css_system_end[]

    asm("_binary_system_css_end");

extern const uint8_t js_system_start[]

    asm("_binary_system_js_start");

extern const uint8_t js_system_end[]

    asm("_binary_system_js_end");


extern const uint8_t station_modal_html_start[]
    asm("_binary_station_modal_html_start");

extern const uint8_t station_modal_html_end[]
    asm("_binary_station_modal_html_end");

extern const uint8_t player_html_start[]
    asm("_binary_player_html_start");

extern const uint8_t player_html_end[]
    asm("_binary_player_html_end");

extern const uint8_t stations_html_start[]
    asm("_binary_stations_html_start");

extern const uint8_t stations_html_end[]
    asm("_binary_stations_html_end");

extern const uint8_t audio_html_start[]
    asm("_binary_audio_html_start");

extern const uint8_t audio_html_end[]
    asm("_binary_audio_html_end");

extern const uint8_t wifi_html_start[]
    asm("_binary_wifi_html_start");

extern const uint8_t wifi_html_end[]
    asm("_binary_wifi_html_end");

extern const uint8_t system_html_start[]
    asm("_binary_system_html_start");

extern const uint8_t system_html_end[]
    asm("_binary_system_html_end");
extern const uint8_t css_audio_start[]
    asm("_binary_audio_css_start");

extern const uint8_t css_audio_end[]
    asm("_binary_audio_css_end");

extern const uint8_t js_audio_start[]
    asm("_binary_audio_js_start");

extern const uint8_t js_audio_end[]
    asm("_binary_audio_js_end");
static const char *TAG = "web_server";

static esp_err_t receive_request_body(
    httpd_req_t *request,
    char *buffer,
    size_t buffer_size
);

static esp_err_t send_json(
    httpd_req_t *request,
    const char *json
);

static bool json_get_u32(
    const char *json,
    const char *key,
    uint32_t *value
);

static bool json_get_string(
    const char *json,
    const char *key,
    char *output,
    size_t output_size
);

static bool json_get_bool(
    const char *json,
    const char *key,
    bool default_value
);

static httpd_handle_t server = NULL;
static esp_err_t api_stations_update_handler(
    httpd_req_t *request
)
{
    char body[512];

    esp_err_t result =
        receive_request_body(
            request,
            body,
            sizeof(body)
        );

    if (result != ESP_OK) {
        return result;
    }

    radio_station_t station = {0};

    if (!json_get_u32(
            body,
            "id",
            &station.id
        )) {

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Ungueltige Sender-ID"
        );

        return ESP_ERR_INVALID_ARG;
    }

    if (!json_get_string(
            body,
            "name",
            station.name,
            sizeof(station.name)
        )) {

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Sendername fehlt oder ist ungueltig"
        );

        return ESP_ERR_INVALID_ARG;
    }

    if (!json_get_string(
            body,
            "url",
            station.url,
            sizeof(station.url)
        )) {

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Stream-URL fehlt oder ist ungueltig"
        );

        return ESP_ERR_INVALID_ARG;
    }

    station.favorite =
        json_get_bool(
            body,
            "favorite",
            false
        );

    result =
        station_manager_update(
            &station
        );

    if (result == ESP_ERR_NOT_FOUND) {
        httpd_resp_send_err(
            request,
            HTTPD_404_NOT_FOUND,
            "Sender nicht gefunden"
        );

        return result;
    }

    if (result == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Ungueltige Senderdaten"
        );

        return result;
    }

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Sender %u konnte nicht aktualisiert werden: %s",
            (unsigned int)station.id,
            esp_err_to_name(result)
        );

        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Sender konnte nicht aktualisiert werden"
        );

        return result;
    }

    return send_json(
        request,
        "{"
            "\"ok\":true,"
            "\"updated\":true"
        "}"
    );
}
static esp_err_t api_stations_delete_handler(
    httpd_req_t *request
)
{
    char body[128];

    esp_err_t result =
        receive_request_body(
            request,
            body,
            sizeof(body)
        );

    if (result != ESP_OK) {
        return result;
    }

    uint32_t station_id = 0;

    if (!json_get_u32(
            body,
            "id",
            &station_id
        )) {

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Ungueltige Sender-ID"
        );

        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(
        TAG,
        "Loesche Sender ID %u",
        (unsigned int)station_id
    );

    result =
        station_manager_delete(
            station_id
        );

    if (result == ESP_ERR_NOT_FOUND) {
        httpd_resp_send_err(
            request,
            HTTPD_404_NOT_FOUND,
            "Sender nicht gefunden"
        );

        return result;
    }

    if (result == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(
            request,
            "409 Conflict"
        );

        return send_json(
            request,
            "{"
                "\"ok\":false,"
                "\"error\":\"Der letzte Sender kann nicht geloescht werden\""
            "}"
        );
    }

    if (result == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Ungueltige Sender-ID"
        );

        return result;
    }

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Sender %u konnte nicht geloescht werden: %s",
            (unsigned int)station_id,
            esp_err_to_name(result)
        );

        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Sender konnte nicht geloescht werden"
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "Sender ID %u erfolgreich geloescht",
        (unsigned int)station_id
    );

    return send_json(
        request,
        "{"
            "\"ok\":true,"
            "\"deleted\":true"
        "}"
    );
}
static esp_err_t api_wifi_scan_post_handler(
    httpd_req_t *request
)
{
    wifi_manager_scan_result_t results[
        WIFI_MANAGER_MAX_SCAN_RESULTS
    ];

    size_t result_count = 0;

    esp_err_t result =
        wifi_manager_scan(
            results,
            WIFI_MANAGER_MAX_SCAN_RESULTS,
            &result_count
        );

    if (result != ESP_OK) {
        httpd_resp_set_status(
            request,
            "500 Internal Server Error"
        );

        return httpd_resp_sendstr(
            request,
            "{\"error\":\"WLAN-Scan fehlgeschlagen\"}"
        );
    }

    cJSON *root =
        cJSON_CreateObject();

    cJSON *networks =
        cJSON_CreateArray();

    if (root == NULL || networks == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(networks);

        return ESP_ERR_NO_MEM;
    }

    for (size_t index = 0;
         index < result_count;
         index++) {

        cJSON *network =
            cJSON_CreateObject();

        if (network == NULL) {
            continue;
        }

        cJSON_AddStringToObject(
            network,
            "ssid",
            results[index].ssid
        );

        cJSON_AddNumberToObject(
            network,
            "rssi",
            results[index].rssi
        );

        cJSON_AddNumberToObject(
            network,
            "channel",
            results[index].channel
        );

        cJSON_AddBoolToObject(
            network,
            "hidden",
            results[index].ssid[0] == '\0'
        );

        const char *security =
            results[index].authmode ==
                WIFI_AUTH_OPEN
                ? "Offen"
                : results[index].authmode ==
                    WIFI_AUTH_WPA3_PSK
                    ? "WPA3"
                    : results[index].authmode ==
                        WIFI_AUTH_WPA2_WPA3_PSK
                        ? "WPA2/WPA3"
                        : "WPA2";

        cJSON_AddStringToObject(
            network,
            "security",
            security
        );

        cJSON_AddItemToArray(
            networks,
            network
        );
    }

    cJSON_AddItemToObject(
        root,
        "networks",
        networks
    );

    char *response =
        cJSON_PrintUnformatted(root);

    cJSON_Delete(root);

    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(
        request,
        "application/json"
    );

    result =
        httpd_resp_sendstr(
            request,
            response
        );

    free(response);

    return result;
}

static esp_err_t api_wifi_connect_post_handler(
    httpd_req_t *request
)
{
    char body[512];

    esp_err_t result =
        receive_request_body(
            request,
            body,
            sizeof(body)
        );

    if (result != ESP_OK) {
        return result;
    }

    cJSON *root =
        cJSON_Parse(body);

    if (root == NULL) {
        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Ungueltiges JSON"
        );

        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *ssid_item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "ssid"
        );

    const cJSON *password_item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "password"
        );

    const cJSON *save_item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "save"
        );

    /*
     * Je nach wifi.js kann das Feld auch
     * save_network heißen.
     */
    if (save_item == NULL) {
        save_item =
            cJSON_GetObjectItemCaseSensitive(
                root,
                "save_network"
            );
    }

    if (!cJSON_IsString(ssid_item) ||
        ssid_item->valuestring == NULL ||
        ssid_item->valuestring[0] == '\0') {

        cJSON_Delete(root);

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "SSID fehlt"
        );

        return ESP_ERR_INVALID_ARG;
    }

    const char *password = "";

    if (cJSON_IsString(password_item) &&
        password_item->valuestring != NULL) {

        password =
            password_item->valuestring;
    }

    const bool save_network =
        cJSON_IsTrue(save_item);

    /*
     * 20 Sekunden reichen normalerweise.
     * Währenddessen wartet nur diese HTTP-Anfrage.
     */
    result =
        wifi_manager_connect(
            ssid_item->valuestring,
            password,
            save_network,
            20000U
        );

    cJSON_Delete(root);

    if (result == ESP_OK) {
        httpd_resp_set_type(
            request,
            "application/json; charset=utf-8"
        );

        return httpd_resp_sendstr(
            request,
            "{\"success\":true,\"connected\":true}"
        );
    }

    if (result == ESP_ERR_TIMEOUT) {
        httpd_resp_set_status(
            request,
            "408 Request Timeout"
        );

        httpd_resp_set_type(
            request,
            "application/json; charset=utf-8"
        );

        return httpd_resp_sendstr(
            request,
            "{\"success\":false,\"error\":\"Verbindung fehlgeschlagen oder Zeitueberschreitung\"}"
        );
    }

    httpd_resp_set_status(
        request,
        "500 Internal Server Error"
    );

    httpd_resp_set_type(
        request,
        "application/json; charset=utf-8"
    );

    return httpd_resp_sendstr(
        request,
        "{\"success\":false,\"error\":\"WLAN-Verbindung konnte nicht hergestellt werden\"}"
    );
}

static const char *wifi_authmode_to_string(
    wifi_auth_mode_t authmode
)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        return "Offen";

    case WIFI_AUTH_WEP:
        return "WEP";

    case WIFI_AUTH_WPA_PSK:
        return "WPA";

    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";

    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";

    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";

    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";

    default:
        return "Unbekannt";
    }
}

static esp_err_t send_wifi_json(
    httpd_req_t *request,
    cJSON *root
)
{
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char *json =
        cJSON_PrintUnformatted(root);

    cJSON_Delete(root);

    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(
        request,
        "application/json; charset=utf-8"
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store"
    );

    esp_err_t result =
        httpd_resp_sendstr(
            request,
            json
        );

    free(json);

    return result;
}

static esp_err_t api_wifi_status_get_handler(
    httpd_req_t *request
)
{
    wifi_manager_status_t status = {0};

    esp_err_t result =
        wifi_manager_get_status(
            &status
        );

    if (result != ESP_OK) {
        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "WLAN-Status konnte nicht gelesen werden"
        );

        return result;
    }

    cJSON *root =
        cJSON_CreateObject();

    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddBoolToObject(
        root,
        "connected",
        status.station_connected
    );

    cJSON_AddBoolToObject(
        root,
        "connecting",
        status.station_connecting
    );

    cJSON_AddStringToObject(
        root,
        "ssid",
        status.station_ssid
    );

    cJSON_AddNumberToObject(
        root,
        "rssi",
        status.rssi
    );

    cJSON_AddNumberToObject(
        root,
        "channel",
        status.channel
    );

    cJSON_AddStringToObject(
        root,
        "security",
        wifi_authmode_to_string(
            status.authmode
        )
    );

    cJSON_AddStringToObject(
        root,
        "ip",
        status.ip_address
    );

    cJSON_AddStringToObject(
        root,
        "gateway",
        status.gateway
    );

    cJSON_AddStringToObject(
        root,
        "netmask",
        status.netmask
    );

    cJSON_AddStringToObject(
        root,
        "dns",
        ""
    );

    cJSON_AddStringToObject(
        root,
        "mac",
        ""
    );

    cJSON_AddStringToObject(
        root,
        "hostname",
        "OpenAudio32"
    );

    cJSON_AddBoolToObject(
        root,
        "internet",
        status.internet_reachable
    );

    cJSON_AddNumberToObject(
        root,
        "disconnect_count",
        status.disconnect_count
    );

    cJSON_AddNumberToObject(
        root,
        "reconnect_count",
        status.reconnect_count
    );

    cJSON_AddNumberToObject(
        root,
        "last_disconnect_reason",
        status.last_disconnect_reason
    );

    cJSON *ap =
        cJSON_CreateObject();

    if (ap == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddBoolToObject(
        ap,
        "enabled",
        status.access_point_active
    );

    cJSON_AddStringToObject(
        ap,
        "ip",
        status.ap_ip_address
    );

    cJSON_AddNumberToObject(
        ap,
        "clients",
        0
    );

    cJSON_AddItemToObject(
        root,
        "ap",
        ap
    );

    return send_wifi_json(
        request,
        root
    );
}

static esp_err_t api_wifi_saved_get_handler(
    httpd_req_t *request
)
{
    wifi_manager_network_t networks[
        WIFI_MANAGER_MAX_NETWORKS
    ] = {0};

    size_t network_count = 0;

    esp_err_t result =
        wifi_manager_get_saved_networks(
            networks,
            WIFI_MANAGER_MAX_NETWORKS,
            &network_count
        );

    if (result != ESP_OK &&
        result != ESP_ERR_INVALID_SIZE) {

        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Gespeicherte WLANs konnten nicht gelesen werden"
        );

        return result;
    }

    cJSON *root =
        cJSON_CreateObject();

    cJSON *items =
        cJSON_CreateArray();

    if (root == NULL || items == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(items);
        return ESP_ERR_NO_MEM;
    }

    for (size_t index = 0;
         index < network_count;
         index++) {

        cJSON *item =
            cJSON_CreateObject();

        if (item == NULL) {
            continue;
        }

        cJSON_AddNumberToObject(
            item,
            "id",
            index
        );

        cJSON_AddStringToObject(
            item,
            "ssid",
            networks[index].ssid
        );

        cJSON_AddNumberToObject(
            item,
            "priority",
            networks[index].priority
        );

        cJSON_AddBoolToObject(
            item,
            "auto_connect",
            networks[index].auto_connect
        );

        cJSON_AddBoolToObject(
            item,
            "hidden",
            networks[index].hidden
        );

        cJSON_AddStringToObject(
            item,
            "last_connected",
            ""
        );

        cJSON_AddItemToArray(
            items,
            item
        );
    }

    cJSON_AddItemToObject(
        root,
        "networks",
        items
    );

    return send_wifi_json(
        request,
        root
    );
}

static esp_err_t api_wifi_config_get_handler(
    httpd_req_t *request
)
{
    wifi_manager_ap_config_t config = {0};

    esp_err_t result =
        wifi_manager_get_ap_config(
            &config
        );

    if (result != ESP_OK) {
        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "WLAN-Konfiguration konnte nicht gelesen werden"
        );

        return result;
    }

    cJSON *root =
        cJSON_CreateObject();

    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(
        root,
        "ap_ssid",
        config.ssid
    );

    cJSON_AddStringToObject(
        root,
        "ap_mode",
        config.always_on
            ? "always"
            : "fallback"
    );

    cJSON_AddBoolToObject(
        root,
        "ap_enabled",
        config.enabled
    );

    cJSON_AddNumberToObject(
        root,
        "ap_channel",
        config.channel
    );

    cJSON_AddNumberToObject(
        root,
        "ap_max_clients",
        config.max_clients
    );

    cJSON_AddStringToObject(
        root,
        "hostname",
        "OpenAudio32"
    );

    cJSON_AddBoolToObject(
        root,
        "dhcp",
        true
    );

    return send_wifi_json(
        request,
        root
    );
}

static esp_err_t send_audio_dsp_settings_json(
    httpd_req_t *request,
    const audio_dsp_settings_t *settings
)
{
    if (request == NULL ||
        settings == NULL) {

        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root =
        cJSON_CreateObject();

    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddBoolToObject(
        root,
        "equalizer_enabled",
        settings->enabled
    );

    cJSON_AddNumberToObject(
        root,
        "equalizer_mode",
        10
    );

    cJSON_AddStringToObject(
        root,
        "preset",
        "custom"
    );

    cJSON *bands =
        cJSON_CreateArray();

    if (bands == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    for (size_t index = 0;
         index < AUDIO_DSP_EQ_BANDS;
         index++) {

        cJSON *value =
            cJSON_CreateNumber(
                settings->eq[index]
            );

        if (value == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddItemToArray(
            bands,
            value
        );
    }

    cJSON_AddItemToObject(
        root,
        "bands",
        bands
    );

    cJSON_AddNumberToObject(
        root,
        "bass",
        settings->bass_db
    );

    cJSON_AddNumberToObject(
        root,
        "treble",
        settings->treble_db
    );

    cJSON_AddNumberToObject(
        root,
        "balance",
        settings->balance * 100.0f
    );

    cJSON_AddBoolToObject(
        root,
        "loudness_enabled",
        settings->loudness_enabled
    );

    cJSON_AddBoolToObject(
        root,
        "limiter_enabled",
        settings->limiter_enabled
    );

    cJSON_AddNumberToObject(
        root,
        "limiter_threshold_db",
        settings->limiter_threshold_db
    );

    cJSON_AddNumberToObject(
        root,
        "startup_volume",
        audio_get_volume() * 100.0f
    );

    cJSON_AddNumberToObject(
        root,
        "maximum_volume",
        100
    );

    cJSON_AddStringToObject(
        root,
        "output_mode",
        "stereo"
    );

    cJSON_AddBoolToObject(
        root,
        "swap_channels",
        false
    );

    char *json =
        cJSON_PrintUnformatted(root);

    cJSON_Delete(root);

    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t result =
        send_json(
            request,
            json
        );

    cJSON_free(json);

    return result;
}
static esp_err_t api_audio_settings_get_handler(
    httpd_req_t *request
)
{
    audio_dsp_settings_t settings = {0};

    esp_err_t result =
        audio_dsp_get_settings(
            &settings
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "DSP-Einstellungen konnten nicht gelesen werden: %s",
            esp_err_to_name(result)
        );

        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "DSP-Einstellungen konnten nicht gelesen werden"
        );

        return result;
    }

    return send_audio_dsp_settings_json(
        request,
        &settings
    );
}
static esp_err_t receive_request_body(

    httpd_req_t *request,

    char *buffer,

    size_t buffer_size

);
static esp_err_t api_settings_get_handler(
    httpd_req_t *request
)
{
    openaudio_settings_t settings = {0};

    esp_err_t result =
        settings_get(
            &settings
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Einstellungen konnten nicht gelesen werden: %s",
            esp_err_to_name(result)
        );

        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Einstellungen konnten nicht gelesen werden"
        );

        return result;
    }

    char response[768];

    const int written = snprintf(
        response,
        sizeof(response),
        "{"
            "\"version\":%u,"
            "\"audio_buffer_kib\":%u,"
            "\"audio_block_frames\":%u,"
            "\"mp3_decode_threshold_bytes\":%u,"
            "\"playback_prebuffer_ms\":%u,"
            "\"volume_fade_ms\":%u,"
            "\"http_buffer_bytes\":%u,"
            "\"http_timeout_ms\":%u,"
            "\"reconnect_delay_ms\":%u,"
            "\"maximum_redirects\":%u,"
            "\"icy_metadata_enabled\":%s,"
            "\"underrun_logging_enabled\":%s,"
            "\"restart_required\":%s"
        "}",
        (unsigned int)settings.version,
        (unsigned int)settings.audio_buffer_kib,
        (unsigned int)settings.audio_block_frames,
        (unsigned int)
            settings.mp3_decode_threshold_bytes,
        (unsigned int)
            settings.playback_prebuffer_ms,
        (unsigned int)
            settings.volume_fade_ms,
        (unsigned int)
            settings.http_buffer_bytes,
        (unsigned int)
            settings.http_timeout_ms,
        (unsigned int)
            settings.reconnect_delay_ms,
        (unsigned int)
            settings.maximum_redirects,
        settings.icy_metadata_enabled
            ? "true"
            : "false",
        settings.underrun_logging_enabled
            ? "true"
            : "false",
        settings_restart_required()
            ? "true"
            : "false"
    );

    if (written < 0 ||
        (size_t)written >= sizeof(response)) {

        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "JSON-Antwort ist zu gross"
        );

        return ESP_ERR_INVALID_SIZE;
    }

    return send_json(
        request,
        response
    );
}

/*
 * Diese Symbole werden durch EMBED_FILES in CMake automatisch erzeugt.
 *
 * Pfad:
 * www/index.html
 *
 * wird zu:
 * _binary_www_index_html_start
 * _binary_www_index_html_end
 */
extern const uint8_t index_html_start[]
    asm("_binary_index_html_start");

extern const uint8_t index_html_end[]
    asm("_binary_index_html_end");

static bool json_get_u32(
    const char *json,
    const char *key,
    uint32_t *value
)

{
    if (json == NULL ||
        key == NULL ||
        value == NULL) {

        return false;
    }

    char search_key[64];

    const int length = snprintf(
        search_key,
        sizeof(search_key),
        "\"%s\"",
        key
    );

    if (length <= 0 ||
        (size_t)length >= sizeof(search_key)) {

        return false;
    }

    const char *position =
        strstr(json, search_key);

    if (position == NULL) {
        return false;
    }

    position =
        strchr(position, ':');

    if (position == NULL) {
        return false;
    }

    position++;

    while (*position == ' ' ||
           *position == '\t') {

        position++;
    }

    char *end = NULL;

    const unsigned long parsed =
        strtoul(
            position,
            &end,
            10
        );

    if (end == position ||
        parsed == 0 ||
        parsed > UINT32_MAX) {

        return false;
    }

    *value = (uint32_t)parsed;

    return true;
}

static esp_err_t send_embedded_file(
    httpd_req_t *request,
    const uint8_t *start,
    const uint8_t *end,
    const char *content_type
)
{
    if (request == NULL ||
        start == NULL ||
        end == NULL ||
        end <= start ||
        content_type == NULL) {

        return ESP_ERR_INVALID_ARG;
    }

    /*
     * EMBED_TXTFILES fügt automatisch ein abschließendes
     * Nullbyte hinzu. Dieses gehört nicht zur HTTP-Antwort.
     */
    const size_t embedded_size =
        (size_t)(end - start);

    if (embedded_size == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t file_size =
        embedded_size - 1U;

    httpd_resp_set_type(
        request,
        content_type
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store, no-cache, must-revalidate"
    );

    return httpd_resp_send(
        request,
        (const char *)start,
        (ssize_t)file_size
    );
}
static bool json_get_u32_allow_zero(
    const char *json,
    const char *key,
    uint32_t *value
)
{
    if (json == NULL ||
        key == NULL ||
        value == NULL) {

        return false;
    }

    char search_key[64];

    const int length = snprintf(
        search_key,
        sizeof(search_key),
        "\"%s\"",
        key
    );

    if (length <= 0 ||
        (size_t)length >= sizeof(search_key)) {

        return false;
    }

    const char *position =
        strstr(json, search_key);

    if (position == NULL) {
        return false;
    }

    position = strchr(
        position,
        ':'
    );

    if (position == NULL) {
        return false;
    }

    position++;

    while (*position == ' ' ||
           *position == '\t' ||
           *position == '\r' ||
           *position == '\n') {

        position++;
    }

    char *end = NULL;

    const unsigned long parsed =
        strtoul(
            position,
            &end,
            10
        );

    if (end == position ||
        parsed > UINT32_MAX) {

        return false;
    }

    *value =
        (uint32_t)parsed;

    return true;
}
static esp_err_t send_json(
    httpd_req_t *request,
    const char *json
)
{
    httpd_resp_set_type(
        request,
        "application/json; charset=utf-8"
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store"
    );

    return httpd_resp_sendstr(
        request,
        json
    );
}

static size_t json_escape_string(
    const char *source,
    char *destination,
    size_t destination_size
)
{
    if (source == NULL ||
        destination == NULL ||
        destination_size == 0) {

        return 0;
    }

    size_t output = 0;

    for (size_t input = 0;
         source[input] != '\0';
         input++) {

        const char character = source[input];
        const char *replacement = NULL;

        switch (character) {
        case '"':
            replacement = "\\\"";
            break;

        case '\\':
            replacement = "\\\\";
            break;

        case '\n':
            replacement = "\\n";
            break;

        case '\r':
            replacement = "\\r";
            break;

        case '\t':
            replacement = "\\t";
            break;

        default:
            break;
        }

        if (replacement != NULL) {
            for (size_t index = 0;
                 replacement[index] != '\0';
                 index++) {

                if (output + 1 >= destination_size) {
                    destination[output] = '\0';
                    return output;
                }

                destination[output++] =
                    replacement[index];
            }
        } else {
            if (output + 1 >= destination_size) {
                destination[output] = '\0';
                return output;
            }

            destination[output++] =
                character;
        }
    }

    destination[output] = '\0';

    return output;
}
static bool json_get_string(
    const char *json,
    const char *key,
    char *output,
    size_t output_size
)
{
    if (json == NULL ||
        key == NULL ||
        output == NULL ||
        output_size == 0) {

        return false;
    }

    char search_key[64];

    const int key_length = snprintf(
        search_key,
        sizeof(search_key),
        "\"%s\"",
        key
    );

    if (key_length <= 0 ||
        (size_t)key_length >= sizeof(search_key)) {

        return false;
    }

    const char *position =
        strstr(json, search_key);

    if (position == NULL) {
        return false;
    }

    position = strchr(position, ':');

    if (position == NULL) {
        return false;
    }

    position++;

    while (*position == ' ' ||
           *position == '\t') {

        position++;
    }

    if (*position != '"') {
        return false;
    }

    position++;

    size_t output_index = 0;

    while (*position != '\0' &&
           *position != '"') {

        char character = *position++;

        if (character == '\\') {
            const char escaped = *position++;

            switch (escaped) {
            case '"':
                character = '"';
                break;

            case '\\':
                character = '\\';
                break;

            case 'n':
                character = '\n';
                break;

            case 'r':
                character = '\r';
                break;

            case 't':
                character = '\t';
                break;

            default:
                return false;
            }
        }

        if (output_index + 1 >= output_size) {
            return false;
        }

        output[output_index++] =
            character;
    }

    if (*position != '"') {
        return false;
    }

    output[output_index] = '\0';

    return output_index > 0;
}
static bool json_get_bool(
    const char *json,
    const char *key,
    bool default_value
)
{
    if (json == NULL || key == NULL) {
        return default_value;
    }

    char search_key[64];

    snprintf(
        search_key,
        sizeof(search_key),
        "\"%s\"",
        key
    );

    const char *position =
        strstr(json, search_key);

    if (position == NULL) {
        return default_value;
    }

    position = strchr(position, ':');

    if (position == NULL) {
        return default_value;
    }

    position++;

    while (*position == ' ' ||
           *position == '\t') {

        position++;
    }

    if (strncmp(position, "true", 4) == 0) {
        return true;
    }

    if (strncmp(position, "false", 5) == 0) {
        return false;
    }

    return default_value;
}
static esp_err_t api_stations_get_handler(
    httpd_req_t *request
)
{
    httpd_resp_set_type(
        request,
        "application/json; charset=utf-8"
    );

    httpd_resp_set_hdr(
        request,
        "Cache-Control",
        "no-store"
    );

    ESP_RETURN_ON_ERROR(
        httpd_resp_send_chunk(
            request,
            "{\"stations\":[",
            HTTPD_RESP_USE_STRLEN
        ),
        TAG,
        "JSON-Start konnte nicht gesendet werden"
    );

    const size_t station_count =
        station_manager_get_count();

    const uint32_t current_id =
        station_manager_get_current_id();

    for (size_t index = 0;
         index < station_count;
         index++) {

        radio_station_t station;

        esp_err_t result =
            station_manager_get(
                index,
                &station
            );

        if (result != ESP_OK) {
            continue;
        }

        char escaped_name[
            STATION_NAME_MAX_LENGTH * 2
        ];

        char escaped_url[
            STATION_URL_MAX_LENGTH * 2
        ];

        json_escape_string(
            station.name,
            escaped_name,
            sizeof(escaped_name)
        );

        json_escape_string(
            station.url,
            escaped_url,
            sizeof(escaped_url)
        );

        char station_json[768];

        snprintf(
            station_json,
            sizeof(station_json),
            "%s{"
                "\"id\":%u,"
                "\"name\":\"%s\","
                "\"url\":\"%s\","
                "\"favorite\":%s,"
                "\"current\":%s"
            "}",
            index > 0 ? "," : "",
            (unsigned int)station.id,
            escaped_name,
            escaped_url,
            station.favorite
                ? "true"
                : "false",
            station.id == current_id
                ? "true"
                : "false"
        );

        ESP_RETURN_ON_ERROR(
            httpd_resp_send_chunk(
                request,
                station_json,
                HTTPD_RESP_USE_STRLEN
            ),
            TAG,
            "Sender konnte nicht gesendet werden"
        );
    }

    ESP_RETURN_ON_ERROR(
        httpd_resp_send_chunk(
            request,
            "]}",
            HTTPD_RESP_USE_STRLEN
        ),
        TAG,
        "JSON-Ende konnte nicht gesendet werden"
    );

    return httpd_resp_send_chunk(
        request,
        NULL,
        0
    );
}
static esp_err_t api_stations_add_handler(
    httpd_req_t *request
)
{
    char body[512];

    esp_err_t result =
        receive_request_body(
            request,
            body,
            sizeof(body)
        );

    if (result != ESP_OK) {
        return result;
    }

    radio_station_t station = {0};

    if (!json_get_string(
            body,
            "name",
            station.name,
            sizeof(station.name)
        )) {

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Sendername fehlt oder ist ungueltig"
        );

        return ESP_ERR_INVALID_ARG;
    }

    if (!json_get_string(
            body,
            "url",
            station.url,
            sizeof(station.url)
        )) {

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Stream-URL fehlt oder ist ungueltig"
        );

        return ESP_ERR_INVALID_ARG;
    }

    station.favorite =
        json_get_bool(
            body,
            "favorite",
            false
        );

    result = station_manager_add(
        &station
    );

    if (result == ESP_ERR_NO_MEM) {
        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Maximale Senderanzahl erreicht"
        );

        return result;
    }

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Sender konnte nicht angelegt werden: %s",
            esp_err_to_name(result)
        );

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Sender konnte nicht gespeichert werden"
        );

        return result;
    }

    char escaped_name[
        STATION_NAME_MAX_LENGTH * 2
    ];

    json_escape_string(
        station.name,
        escaped_name,
        sizeof(escaped_name)
    );

    char response[256];

    snprintf(
        response,
        sizeof(response),
        "{"
            "\"ok\":true,"
            "\"id\":%u,"
            "\"name\":\"%s\""
        "}",
        (unsigned int)station.id,
        escaped_name
    );

    return send_json(
        request,
        response
    );
}
static esp_err_t api_stations_select_handler(
    httpd_req_t *request
)
{
    char body[128];

    esp_err_t result =
        receive_request_body(
            request,
            body,
            sizeof(body)
        );

    if (result != ESP_OK) {
        return result;
    }

    uint32_t station_id = 0;

    if (!json_get_u32(
            body,
            "id",
            &station_id
        )) {

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Ungueltige Sender-ID"
        );

        return ESP_ERR_INVALID_ARG;
    }

    result =
        radio_player_play_station(
            station_id
        );

    if (result == ESP_ERR_NOT_FOUND) {
        httpd_resp_send_err(
            request,
            HTTPD_404_NOT_FOUND,
            "Sender nicht gefunden"
        );

        return result;
    }

    if (result == ESP_ERR_TIMEOUT) {
        ESP_LOGE(
            TAG,
            "Laufender Stream konnte nicht rechtzeitig beendet werden"
        );

        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Alter Stream konnte nicht beendet werden"
        );

        return result;
    }

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Senderwechsel fehlgeschlagen: %s",
            esp_err_to_name(result)
        );

        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Senderwechsel fehlgeschlagen"
        );

        return result;
    }

    radio_station_t station = {0};

    result =
        station_manager_get_by_id(
            station_id,
            &station
        );

    if (result != ESP_OK) {
        return result;
    }

    char escaped_name[
        STATION_NAME_MAX_LENGTH * 2
    ];

    json_escape_string(
        station.name,
        escaped_name,
        sizeof(escaped_name)
    );

    char response[256];

    snprintf(
        response,
        sizeof(response),
        "{"
            "\"ok\":true,"
            "\"id\":%u,"
            "\"name\":\"%s\""
        "}",
        (unsigned int)station_id,
        escaped_name
    );

    return send_json(
        request,
        response
    );
}

static esp_err_t api_status_handler(
    httpd_req_t *request
)
{
    wifi_ap_record_t ap_info = {0};

    int rssi = 0;

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    const unsigned long long uptime_seconds =
        (unsigned long long)(
            esp_timer_get_time() / 1000000ULL
        );

    const unsigned int volume_percent =
        (unsigned int)(
            audio_get_volume() * 100.0f + 0.5f
        );
openaudio_settings_t current_settings = {0};

const esp_err_t settings_result =
    settings_get(
        &current_settings
    );

const uint32_t buffer_kib =
    settings_result == ESP_OK
        ? current_settings.audio_buffer_kib
        : 128U;
    radio_station_t current_station = {0};

    const esp_err_t station_result =
        station_manager_get_current(
            &current_station
        );

    char escaped_station_name[
        STATION_NAME_MAX_LENGTH * 2
    ];

    if (station_result == ESP_OK) {
        json_escape_string(
            current_station.name,
            escaped_station_name,
            sizeof(escaped_station_name)
        );
    } else {
        snprintf(
            escaped_station_name,
            sizeof(escaped_station_name),
            "%s",
            "Unbekannter Sender"
        );
    }

    char response[768];

    snprintf(
    response,
    sizeof(response),
    "{"
        "\"station\":\"%s\","
        "\"station_id\":%u,"
        "\"track\":\"Titelinformationen werden geladen\","
        "\"codec\":\"MP3\","
        "\"bitrate\":128,"
        "\"sample_rate\":%u,"
        "\"channels\":2,"
        "\"volume\":%u,"
        "\"muted\":%s,"
        "\"playing\":%s,"
        "\"rssi\":%d,"
        "\"uptime\":%llu,"
        "\"buffer_kib\":%u,"
        "\"underruns\":%u"
    "}",
    escaped_station_name,
    (unsigned int)
        station_manager_get_current_id(),
    (unsigned int)
        audio_get_sample_rate(),
    volume_percent,
    audio_is_muted()
        ? "true"
        : "false",
    radio_player_is_running()
        ? "true"
        : "false",
    rssi,
    uptime_seconds,
    (unsigned int)buffer_kib,
    (unsigned int)
        audio_get_underrun_count()
);

    return send_json(
        request,
        response
    );
}
static esp_err_t api_settings_post_handler(
    httpd_req_t *request
)
{
    char body[1024];

    esp_err_t result =
        receive_request_body(
            request,
            body,
            sizeof(body)
        );

    if (result != ESP_OK) {
        return result;
    }

    /*
     * Zuerst die bestehenden Einstellungen laden.
     *
     * Dadurch können später einzelne Felder ausgelassen werden,
     * ohne dass alle übrigen Werte auf null gesetzt werden.
     */
    openaudio_settings_t settings = {0};

    result =
        settings_get(
            &settings
        );

    if (result != ESP_OK) {
        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Aktuelle Einstellungen konnten nicht gelesen werden"
        );

        return result;
    }

    uint32_t value = 0;

    if (json_get_u32_allow_zero(
            body,
            "audio_buffer_kib",
            &value
        )) {

        settings.audio_buffer_kib =
            value;
    }

    if (json_get_u32_allow_zero(
            body,
            "audio_block_frames",
            &value
        )) {

        settings.audio_block_frames =
            value;
    }

    if (json_get_u32_allow_zero(
            body,
            "mp3_decode_threshold_bytes",
            &value
        )) {

        settings.mp3_decode_threshold_bytes =
            value;
    }

    if (json_get_u32_allow_zero(
            body,
            "playback_prebuffer_ms",
            &value
        )) {

        settings.playback_prebuffer_ms =
            value;
    }

    if (json_get_u32_allow_zero(
            body,
            "volume_fade_ms",
            &value
        )) {

        settings.volume_fade_ms =
            value;
    }

    if (json_get_u32_allow_zero(
            body,
            "http_buffer_bytes",
            &value
        )) {

        settings.http_buffer_bytes =
            value;
    }

    if (json_get_u32_allow_zero(
            body,
            "http_timeout_ms",
            &value
        )) {

        settings.http_timeout_ms =
            value;
    }

    if (json_get_u32_allow_zero(
            body,
            "reconnect_delay_ms",
            &value
        )) {

        settings.reconnect_delay_ms =
            value;
    }

    if (json_get_u32_allow_zero(
            body,
            "maximum_redirects",
            &value
        )) {

        settings.maximum_redirects =
            value;
    }

    settings.icy_metadata_enabled =
        json_get_bool(
            body,
            "icy_metadata_enabled",
            settings.icy_metadata_enabled
        );

    settings.underrun_logging_enabled =
        json_get_bool(
            body,
            "underrun_logging_enabled",
            settings.underrun_logging_enabled
        );

    /*
     * Die Version darf nicht aus dem Browser übernommen werden.
     */
    settings.version =
        OPENAUDIO_SETTINGS_VERSION;

    result =
        settings_update(
            &settings
        );

    if (result == ESP_ERR_INVALID_ARG ||
        result == ESP_ERR_INVALID_VERSION) {

        ESP_LOGW(
            TAG,
            "Ungueltige Einstellungen empfangen"
        );

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Mindestens ein Einstellungswert ist ungueltig"
        );

        return result;
    }

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Einstellungen konnten nicht gespeichert werden: %s",
            esp_err_to_name(result)
        );

        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Einstellungen konnten nicht gespeichert werden"
        );

        return result;
    }

    char response[128];

    snprintf(
        response,
        sizeof(response),
        "{"
            "\"ok\":true,"
            "\"restart_required\":%s"
        "}",
        settings_restart_required()
            ? "true"
            : "false"
    );

    ESP_LOGI(
        TAG,
        "Einstellungen ueber Webinterface gespeichert"
    );

    return send_json(
        request,
        response
    );
}
static esp_err_t api_system_restart_handler(
    httpd_req_t *request
)
{
    ESP_LOGW(
        TAG,
        "Neustart über Webinterface angefordert"
    );

    esp_err_t result = send_json(
        request,
        "{"
            "\"ok\":true,"
            "\"restarting\":true"
        "}"
    );

    if (result != ESP_OK) {
        return result;
    }

    /*
     * Kurze Verzögerung, damit die HTTP-Antwort
     * noch vollständig zum Browser übertragen wird.
     */
    vTaskDelay(
        pdMS_TO_TICKS(500)
    );

    esp_restart();

    return ESP_OK;
}
static esp_err_t receive_request_body(
    httpd_req_t *request,
    char *buffer,
    size_t buffer_size
)

{
    if (request->content_len <= 0 ||
        request->content_len >= buffer_size) {

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Ungueltiger Request-Body"
        );

        return ESP_ERR_INVALID_SIZE;
    }

    size_t received_total = 0;

    while (received_total <
           (size_t)request->content_len) {

        const int received = httpd_req_recv(
            request,
            buffer + received_total,
            request->content_len - received_total
        );

        if (received <= 0) {
            httpd_resp_send_err(
                request,
                HTTPD_500_INTERNAL_SERVER_ERROR,
                "Request konnte nicht gelesen werden"
            );

            return ESP_FAIL;
        }

        received_total += (size_t)received;
    }

    buffer[received_total] = '\0';

    return ESP_OK;
}

static esp_err_t api_volume_handler(
    httpd_req_t *request
)
{
    char body[64];

    esp_err_t result = receive_request_body(
        request,
        body,
        sizeof(body)
    );

    if (result != ESP_OK) {
        return result;
    }

    char *value_position =
        strstr(body, "\"volume\"");

    if (value_position == NULL) {
        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "volume fehlt"
        );

        return ESP_ERR_INVALID_ARG;
    }

    value_position = strchr(
        value_position,
        ':'
    );

    if (value_position == NULL) {
        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Ungueltiges JSON"
        );

        return ESP_ERR_INVALID_ARG;
    }

    const long volume =
        strtol(value_position + 1, NULL, 10);

    if (volume < 0 || volume > 100) {
        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Lautstaerke muss zwischen 0 und 100 liegen"
        );

        return ESP_ERR_INVALID_ARG;
    }

    audio_set_volume(
        (float)volume / 100.0f
    );

    char response[64];

    snprintf(
        response,
        sizeof(response),
        "{\"ok\":true,\"volume\":%ld}",
        volume
    );

    return send_json(
        request,
        response
    );
}
static esp_err_t api_mute_handler(
    httpd_req_t *request
)
{
    char body[64];

    esp_err_t result = receive_request_body(
        request,
        body,
        sizeof(body)
    );

    if (result != ESP_OK) {
        return result;
    }

    const bool mute =
        strstr(body, "true") != NULL;

    audio_set_mute(mute);

    return send_json(
        request,
        mute
            ? "{\"ok\":true,\"muted\":true}"
            : "{\"ok\":true,\"muted\":false}"
    );
}

static esp_err_t index_handler(httpd_req_t *request)
{
    return send_embedded_file(
        request,
        index_html_start,
        index_html_end,
        "text/html; charset=utf-8"
    );
}
static esp_err_t player_page_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        player_html_start,
        player_html_end,
        "text/html; charset=utf-8"
    );
}
static esp_err_t stations_page_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        stations_html_start,
        stations_html_end,
        "text/html; charset=utf-8"
    );
}
static esp_err_t audio_page_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        audio_html_start,
        audio_html_end,
        "text/html; charset=utf-8"
    );
}
static esp_err_t wifi_page_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        wifi_html_start,
        wifi_html_end,
        "text/html; charset=utf-8"
    );
}

static esp_err_t system_page_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        system_html_start,
        system_html_end,
        "text/html; charset=utf-8"
    );
}


static esp_err_t station_modal_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        station_modal_html_start,
        station_modal_html_end,
        "text/html; charset=utf-8"
    );
}
static esp_err_t css_wifi_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        css_wifi_start,
        css_wifi_end,
        "text/css; charset=utf-8"
    );
}
static esp_err_t css_app_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        css_app_start,
        css_app_end,
        "text/css; charset=utf-8"
    );
}

static esp_err_t css_player_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        css_player_start,
        css_player_end,
        "text/css; charset=utf-8"
    );
}

static esp_err_t css_stations_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        css_stations_start,
        css_stations_end,
        "text/css; charset=utf-8"
    );
}
static esp_err_t css_system_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        css_system_start,
        css_system_end,
        "text/css; charset=utf-8"
    );
}
static esp_err_t css_audio_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        css_audio_start,
        css_audio_end,
        "text/css; charset=utf-8"
    );
}
static esp_err_t js_system_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        js_system_start,
        js_system_end,
        "application/javascript; charset=utf-8"
    );
}
static esp_err_t js_audio_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        js_audio_start,
        js_audio_end,
        "application/javascript; charset=utf-8"
    );
}

static esp_err_t js_api_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        js_api_start,
        js_api_end,
        "application/javascript; charset=utf-8"
    );
}

static esp_err_t js_player_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        js_player_start,
        js_player_end,
        "application/javascript; charset=utf-8"
    );
}

static esp_err_t js_stations_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        js_stations_start,
        js_stations_end,
        "application/javascript; charset=utf-8"
    );
}

static esp_err_t js_app_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        js_app_start,
        js_app_end,
        "application/javascript; charset=utf-8"
    );
}

static esp_err_t js_wifi_handler(
    httpd_req_t *request
)
{
    return send_embedded_file(
        request,
        js_wifi_start,
        js_wifi_end,
        "application/javascript; charset=utf-8"
    );
}

static esp_err_t api_audio_settings_post_handler(
    httpd_req_t *request
)
{
    char body[1536];

    esp_err_t result =
        receive_request_body(
            request,
            body,
            sizeof(body)
        );

    if (result != ESP_OK) {
        return result;
    }

    cJSON *root =
        cJSON_Parse(body);

    if (root == NULL) {
        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Ungueltiges JSON"
        );

        return ESP_ERR_INVALID_ARG;
    }

    audio_dsp_settings_t settings = {0};

    result =
        audio_dsp_get_settings(
            &settings
        );
            if (result != ESP_OK) {
        cJSON_Delete(root);

        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Aktuelle DSP-Einstellungen konnten nicht gelesen werden"
        );

        return result;
    }
const cJSON *bands =
    cJSON_GetObjectItemCaseSensitive(
        root,
        "bands"
    );

if (bands != NULL) {
    if (!cJSON_IsArray(bands) ||
        cJSON_GetArraySize(bands) !=
            AUDIO_DSP_EQ_BANDS) {

        cJSON_Delete(root);

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Es werden genau 10 EQ-Baender erwartet"
        );

        return ESP_ERR_INVALID_ARG;
    }

    for (size_t index = 0;
         index < AUDIO_DSP_EQ_BANDS;
         index++) {

        const cJSON *item =
            cJSON_GetArrayItem(
                bands,
                (int)index
            );

        if (!cJSON_IsNumber(item) ||
            !isfinite(item->valuedouble) ||
            item->valuedouble < -12.0 ||
            item->valuedouble > 12.0) {

            cJSON_Delete(root);

            httpd_resp_send_err(
                request,
                HTTPD_400_BAD_REQUEST,
                "Ungueltiger EQ-Wert"
            );

            return ESP_ERR_INVALID_ARG;
        }

        settings.eq[index] =
            (float)item->valuedouble;
    }
}


    const cJSON *equalizer_enabled =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "equalizer_enabled"
        );

    if (cJSON_IsBool(equalizer_enabled)) {
        settings.enabled =
            cJSON_IsTrue(equalizer_enabled);
    }

    const cJSON *bass =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "bass"
        );

    if (cJSON_IsNumber(bass)) {
        settings.bass_db =
            (float)bass->valuedouble;
    }

    const cJSON *treble =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "treble"
        );

    if (cJSON_IsNumber(treble)) {
        settings.treble_db =
            (float)treble->valuedouble;
    }

    const cJSON *balance =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "balance"
        );

    if (cJSON_IsNumber(balance)) {
        settings.balance =
            (float)balance->valuedouble /
            100.0f;
    }

    const cJSON *loudness_enabled =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "loudness_enabled"
        );

    if (cJSON_IsBool(loudness_enabled)) {
        settings.loudness_enabled =
            cJSON_IsTrue(loudness_enabled);
    }

    const cJSON *limiter_enabled =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "limiter_enabled"
        );

    if (cJSON_IsBool(limiter_enabled)) {
        settings.limiter_enabled =
            cJSON_IsTrue(limiter_enabled);
    }

    const cJSON *limiter_threshold =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "limiter_threshold_db"
        );

    if (cJSON_IsNumber(limiter_threshold)) {
        settings.limiter_threshold_db =
            (float)limiter_threshold->valuedouble;
    }

    const cJSON *startup_volume =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "startup_volume"
        );

    const cJSON *maximum_volume =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "maximum_volume"
        );

    if (cJSON_IsNumber(startup_volume)) {
        double volume =
            startup_volume->valuedouble;

        double maximum =
            cJSON_IsNumber(maximum_volume)
                ? maximum_volume->valuedouble
                : 100.0;

        if (volume < 0.0 ||
            volume > 100.0 ||
            maximum < 10.0 ||
            maximum > 100.0 ||
            volume > maximum) {

            cJSON_Delete(root);

            httpd_resp_send_err(
                request,
                HTTPD_400_BAD_REQUEST,
                "Ungueltige Lautstaerkeeinstellung"
            );

            return ESP_ERR_INVALID_ARG;
        }

        audio_set_volume(
            (float)(
                volume / 100.0
            )
        );
    }

    result =
        audio_dsp_set_settings(
            &settings
        );

    cJSON_Delete(root);

    if (result == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Mindestens ein DSP-Wert ist ungueltig"
        );

        return result;
    }

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "DSP-Einstellungen konnten nicht uebernommen werden: %s",
            esp_err_to_name(result)
        );

        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "DSP-Einstellungen konnten nicht uebernommen werden"
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "DSP aktualisiert: enabled=%d, bass=%.1f, treble=%.1f, balance=%.2f",
        settings.enabled,
        settings.bass_db,
        settings.treble_db,
        settings.balance
    );

    return send_audio_dsp_settings_json(
        request,
        &settings
    );
}
static esp_err_t api_audio_reset_handler(
    httpd_req_t *request
)
{
    esp_err_t result =
        audio_dsp_reset_settings();

    if (result != ESP_OK) {
        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "DSP konnte nicht zurueckgesetzt werden"
        );

        return result;
    }

    audio_set_volume(
        0.20f
    );

    audio_dsp_settings_t settings = {0};

    result =
        audio_dsp_get_settings(
            &settings
        );

    if (result != ESP_OK) {
        httpd_resp_send_err(
            request,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "DSP-Einstellungen konnten nicht gelesen werden"
        );

        return result;
    }

    return send_audio_dsp_settings_json(
        request,
        &settings
    );
}
esp_err_t web_server_start(void)
{
    if (server != NULL) {
        ESP_LOGW(TAG, "Webserver laeuft bereits");
        return ESP_OK;
    }

    httpd_config_t config =
        HTTPD_DEFAULT_CONFIG();

    config.server_port = 80;
    config.max_uri_handlers = 64;
    config.stack_size = 16384;
    config.lru_purge_enable = true;

    ESP_LOGI(
        TAG,
        "Starte Webserver auf Port %u",
        (unsigned int)config.server_port
    );

    esp_err_t result =
        httpd_start(
            &server,
            &config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Webserver konnte nicht gestartet werden: %s",
            esp_err_to_name(result)
        );

        server = NULL;
        return result;
    }
const httpd_uri_t api_audio_reset_uri = {
    .uri = "/api/audio/reset",
    .method = HTTP_POST,
    .handler = api_audio_reset_handler,
    .user_ctx = NULL,
};
 const httpd_uri_t api_audio_settings_get_uri = {
    .uri = "/api/audio/settings",
    .method = HTTP_GET,
    .handler = api_audio_settings_get_handler,
    .user_ctx = NULL,
};
const httpd_uri_t js_wifi_uri = {
    .uri = "/js/wifi.js",
    .method = HTTP_GET,
    .handler = js_wifi_handler,
    .user_ctx = NULL,
};

const httpd_uri_t api_audio_settings_post_uri = {
    .uri = "/api/audio/settings",
    .method = HTTP_POST,
    .handler = api_audio_settings_post_handler,
    .user_ctx = NULL,
};
const httpd_uri_t station_modal_uri = {
    .uri = "/pages/station-modal.html",
    .method = HTTP_GET,
    .handler = station_modal_handler,
    .user_ctx = NULL,
};
    const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL,
    };
const httpd_uri_t css_audio_uri = {
    .uri = "/css/audio.css",
    .method = HTTP_GET,
    .handler = css_audio_handler,
    .user_ctx = NULL,
};
const httpd_uri_t css_wifi_uri = {
    .uri = "/css/wifi.css",
    .method = HTTP_GET,
    .handler = css_wifi_handler,
    .user_ctx = NULL,
};
const httpd_uri_t js_audio_uri = {
    .uri = "/js/audio.js",
    .method = HTTP_GET,
    .handler = js_audio_handler,
    .user_ctx = NULL,
};
    const httpd_uri_t css_app_uri = {
        .uri = "/css/app.css",
        .method = HTTP_GET,
        .handler = css_app_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t css_player_uri = {
        .uri = "/css/player.css",
        .method = HTTP_GET,
        .handler = css_player_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t css_stations_uri = {
        .uri = "/css/stations.css",
        .method = HTTP_GET,
        .handler = css_stations_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t css_system_uri = {
    .uri = "/css/system.css",
    .method = HTTP_GET,
    .handler = css_system_handler,
    .user_ctx = NULL,
};

const httpd_uri_t js_system_uri = {
    .uri = "/js/system.js",
    .method = HTTP_GET,
    .handler = js_system_handler,
    .user_ctx = NULL,
};
const httpd_uri_t api_system_restart_uri = {
    .uri = "/api/system/restart",
    .method = HTTP_POST,
    .handler = api_system_restart_handler,
    .user_ctx = NULL,
};
const httpd_uri_t player_page_uri = {
    .uri = "/pages/player.html",
    .method = HTTP_GET,
    .handler = player_page_handler,
};

const httpd_uri_t stations_page_uri = {
    .uri = "/pages/stations.html",
    .method = HTTP_GET,
    .handler = stations_page_handler,
};

const httpd_uri_t audio_page_uri = {
    .uri = "/pages/audio.html",
    .method = HTTP_GET,
    .handler = audio_page_handler,
};

const httpd_uri_t wifi_page_uri = {
    .uri = "/pages/wifi.html",
    .method = HTTP_GET,
    .handler = wifi_page_handler,
};



const httpd_uri_t system_page_uri = {
    .uri = "/pages/system.html",
    .method = HTTP_GET,
    .handler = system_page_handler,
};


    const httpd_uri_t js_api_uri = {
        .uri = "/js/api.js",
        .method = HTTP_GET,
        .handler = js_api_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t js_player_uri = {
        .uri = "/js/player.js",
        .method = HTTP_GET,
        .handler = js_player_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t js_stations_uri = {
        .uri = "/js/stations.js",
        .method = HTTP_GET,
        .handler = js_stations_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t js_app_uri = {
        .uri = "/js/app.js",
        .method = HTTP_GET,
        .handler = js_app_handler,
        .user_ctx = NULL,
    };
const httpd_uri_t api_wifi_status_get_uri = {
    .uri = "/api/wifi/status",
    .method = HTTP_GET,
    .handler = api_wifi_status_get_handler,
    .user_ctx = NULL,
};

const httpd_uri_t api_wifi_saved_get_uri = {
    .uri = "/api/wifi/saved",
    .method = HTTP_GET,
    .handler = api_wifi_saved_get_handler,
    .user_ctx = NULL,
};

const httpd_uri_t api_wifi_config_get_uri = {
    .uri = "/api/wifi/config",
    .method = HTTP_GET,
    .handler = api_wifi_config_get_handler,
    .user_ctx = NULL,
};
    /*
     * REST-API
     */
const httpd_uri_t api_wifi_scan_post_uri = {
    .uri = "/api/wifi/scan",
    .method = HTTP_POST,
    .handler = api_wifi_scan_post_handler,
    .user_ctx = NULL,
};
const httpd_uri_t api_wifi_connect_post_uri = {
    .uri = "/api/wifi/connect",
    .method = HTTP_POST,
    .handler = api_wifi_connect_post_handler,
    .user_ctx = NULL,
};
    const httpd_uri_t api_status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_handler,
        .user_ctx = NULL,
    };
const httpd_uri_t api_settings_get_uri = {
    .uri = "/api/settings",
    .method = HTTP_GET,
    .handler = api_settings_get_handler,
    .user_ctx = NULL,
};

const httpd_uri_t api_settings_post_uri = {
    .uri = "/api/settings",
    .method = HTTP_POST,
    .handler = api_settings_post_handler,
    .user_ctx = NULL,
};
    const httpd_uri_t api_volume_uri = {
        .uri = "/api/player/volume",
        .method = HTTP_POST,
        .handler = api_volume_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t api_mute_uri = {
        .uri = "/api/player/mute",
        .method = HTTP_POST,
        .handler = api_mute_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t api_stations_get_uri = {
        .uri = "/api/stations",
        .method = HTTP_GET,
        .handler = api_stations_get_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t api_stations_add_uri = {
        .uri = "/api/stations",
        .method = HTTP_POST,
        .handler = api_stations_add_handler,
        .user_ctx = NULL,
    };
const httpd_uri_t api_stations_update_uri = {
    .uri = "/api/stations/update",
    .method = HTTP_POST,
    .handler = api_stations_update_handler,
    .user_ctx = NULL,
};

const httpd_uri_t api_stations_delete_uri = {
    .uri = "/api/stations/delete",
    .method = HTTP_POST,
    .handler = api_stations_delete_handler,
    .user_ctx = NULL,
};


    const httpd_uri_t api_stations_select_uri = {
        .uri = "/api/stations/select",
        .method = HTTP_POST,
        .handler = api_stations_select_handler,
        .user_ctx = NULL,
    };

    /*
     * Alle Handler zentral registrieren.
     */

const httpd_uri_t *uris[] = {
    &index_uri,

    &player_page_uri,
    &stations_page_uri,
    &audio_page_uri,
    &wifi_page_uri,
    &system_page_uri,
    &station_modal_uri,

    &css_app_uri,
    &css_player_uri,
    &css_stations_uri,
    &css_system_uri,
    &css_audio_uri,
    &css_wifi_uri,

    &js_api_uri,
    &js_player_uri,
    &js_stations_uri,
    &js_system_uri,
    &js_audio_uri,
    &js_app_uri,
    &js_wifi_uri,

    &api_status_uri,
    &api_wifi_status_get_uri,
    &api_wifi_saved_get_uri,
    &api_wifi_config_get_uri,
    &api_wifi_scan_post_uri,
    &api_wifi_connect_post_uri,
    &api_audio_settings_get_uri,
    &api_audio_settings_post_uri,
    &api_audio_reset_uri,
    &api_settings_get_uri,
    &api_settings_post_uri,
    &api_system_restart_uri,
    &api_volume_uri,
    &api_mute_uri,
    &api_stations_get_uri,
    &api_stations_add_uri,
    &api_stations_update_uri,
    &api_stations_delete_uri,
    &api_stations_select_uri,
};

    const size_t uri_count =
        sizeof(uris) / sizeof(uris[0]);

    for (size_t index = 0;
         index < uri_count;
         index++) {

        result = httpd_register_uri_handler(
            server,
            uris[index]
        );

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "URI-Handler %u konnte nicht registriert werden: %s",
                (unsigned int)index,
                esp_err_to_name(result)
            );

            web_server_stop();
            return result;
        }
    }

    ESP_LOGI(
        TAG,
        "Webinterface ist bereit, %u Handler registriert",
        (unsigned int)uri_count
    );

    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (server == NULL) {
        return ESP_OK;
    }

    esp_err_t result =
        httpd_stop(server);

    server = NULL;

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Webserver gestoppt");
    }

    return result;
}