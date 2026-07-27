#include "wifi_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"
#include "nvs_flash.h"

#define WIFI_MANAGER_CONNECTED_BIT BIT0
#define WIFI_MANAGER_FAILED_BIT    BIT1

#define WIFI_MANAGER_NVS_NAMESPACE "wifi_mgr"
#define WIFI_MANAGER_NVS_KEY       "config"
#define WIFI_MANAGER_CONFIG_MAGIC  0x4F413332UL /* OA32 */
#define WIFI_MANAGER_CONFIG_VERSION 2U
#define WIFI_MANAGER_RETRY_LIMIT   5U

static const char *TAG = "wifi_manager";

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t network_count;
    wifi_manager_network_t networks[WIFI_MANAGER_MAX_NETWORKS];
    wifi_manager_ap_config_t ap;
} wifi_manager_persisted_config_t;

typedef struct {
    bool initialized;
    bool started;
    bool station_connected;
    bool station_connecting;
    bool manual_disconnect;
    bool reconnect_enabled;

    uint32_t retry_count;
    uint32_t disconnect_count;
    uint32_t reconnect_count;
    uint32_t last_disconnect_reason;

    char current_ssid[WIFI_MANAGER_SSID_MAX_LENGTH + 1U];

    esp_netif_t *station_netif;
    esp_netif_t *ap_netif;
    EventGroupHandle_t event_group;
    SemaphoreHandle_t mutex;

    esp_event_handler_instance_t wifi_handler;
    esp_event_handler_instance_t ip_handler;

    wifi_manager_persisted_config_t config;
} wifi_manager_context_t;

static wifi_manager_context_t manager = {0};

static void copy_string(char *destination, size_t size, const char *source)
{
    if (destination == NULL || size == 0U) {
        return;
    }

    destination[0] = '\0';

    if (source == NULL) {
        return;
    }

    (void)snprintf(destination, size, "%s", source);
}

static bool valid_ssid(const char *ssid)
{
    if (ssid == NULL) {
        return false;
    }

    const size_t length = strnlen(ssid, WIFI_MANAGER_SSID_MAX_LENGTH + 1U);
    return length > 0U && length <= WIFI_MANAGER_SSID_MAX_LENGTH;
}

static bool valid_password(const char *password)
{
    if (password == NULL) {
        return false;
    }

    return strnlen(password, WIFI_MANAGER_PASSWORD_MAX_LENGTH + 1U)
        <= WIFI_MANAGER_PASSWORD_MAX_LENGTH;
}

static bool valid_ap_config(const wifi_manager_ap_config_t *config)
{
    if (config == NULL || !valid_ssid(config->ssid)) {
        return false;
    }

    const size_t password_length = strnlen(
        config->password,
        WIFI_MANAGER_AP_PASSWORD_MAX_LENGTH + 1U
    );

    if (password_length != 0U &&
        (password_length < 8U || password_length > WIFI_MANAGER_AP_PASSWORD_MAX_LENGTH)) {
        return false;
    }

    if (config->channel < 1U || config->channel > 13U) {
        return false;
    }

    return config->max_clients >= 1U && config->max_clients <= 10U;
}

static esp_err_t lock_manager(TickType_t timeout)
{
    if (manager.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return xSemaphoreTake(manager.mutex, timeout) == pdTRUE
        ? ESP_OK
        : ESP_ERR_TIMEOUT;
}

static void unlock_manager(void)
{
    if (manager.mutex != NULL) {
        xSemaphoreGive(manager.mutex);
    }
}

static void set_default_ap_config(wifi_manager_ap_config_t *config)
{
    uint8_t mac[6] = {0};
    (void)esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    memset(config, 0, sizeof(*config));
    config->enabled = true;
    config->always_on = true;
    config->hidden = false;
    config->channel = WIFI_MANAGER_DEFAULT_AP_CHANNEL;
    config->max_clients = WIFI_MANAGER_DEFAULT_AP_MAX_CLIENTS;

    (void)snprintf(
        config->ssid,
        sizeof(config->ssid),
        "OpenAudio32-%02X%02X",
        mac[4],
        mac[5]
    );

    (void)snprintf(
    config->password,
    sizeof(config->password),
    "%s",
    "openaudio"
);
}

static void set_default_config(wifi_manager_persisted_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->magic = WIFI_MANAGER_CONFIG_MAGIC;
    config->version = WIFI_MANAGER_CONFIG_VERSION;
    set_default_ap_config(&config->ap);
}

static esp_err_t save_config_locked(void)
{
    nvs_handle_t handle = 0;
    ESP_RETURN_ON_ERROR(
        nvs_open(WIFI_MANAGER_NVS_NAMESPACE, NVS_READWRITE, &handle),
        TAG,
        "NVS konnte nicht geöffnet werden"
    );

    esp_err_t result = nvs_set_blob(
        handle,
        WIFI_MANAGER_NVS_KEY,
        &manager.config,
        sizeof(manager.config)
    );

    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }

    nvs_close(handle);
    return result;
}

static esp_err_t load_config_locked(void)
{
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(
        WIFI_MANAGER_NVS_NAMESPACE,
        NVS_READONLY,
        &handle
    );

    if (result == ESP_ERR_NVS_NOT_FOUND) {
        set_default_config(&manager.config);
        return save_config_locked();
    }

    ESP_RETURN_ON_ERROR(result, TAG, "NVS konnte nicht geöffnet werden");

    size_t required_size = sizeof(manager.config);
    result = nvs_get_blob(
        handle,
        WIFI_MANAGER_NVS_KEY,
        &manager.config,
        &required_size
    );
    nvs_close(handle);

    if (result == ESP_ERR_NVS_NOT_FOUND ||
        required_size != sizeof(manager.config) ||
        manager.config.magic != WIFI_MANAGER_CONFIG_MAGIC ||
        manager.config.version != WIFI_MANAGER_CONFIG_VERSION ||
        manager.config.network_count > WIFI_MANAGER_MAX_NETWORKS ||
        !valid_ap_config(&manager.config.ap)) {

        ESP_LOGW(TAG, "WLAN-Konfiguration fehlt oder ist inkompatibel; Standardwerte werden verwendet");
        set_default_config(&manager.config);
        return save_config_locked();
    }

    return result;
}

static int compare_network_priority(const void *left, const void *right)
{
    const wifi_manager_network_t *a = left;
    const wifi_manager_network_t *b = right;

    if (a->priority < b->priority) {
        return -1;
    }
    if (a->priority > b->priority) {
        return 1;
    }
    return strcmp(a->ssid, b->ssid);
}

static int find_network_index_locked(const char *ssid)
{
    for (uint16_t index = 0; index < manager.config.network_count; index++) {
        if (strcmp(manager.config.networks[index].ssid, ssid) == 0) {
            return (int)index;
        }
    }

    return -1;
}

static void normalize_priorities_locked(void)
{
    qsort(
        manager.config.networks,
        manager.config.network_count,
        sizeof(manager.config.networks[0]),
        compare_network_priority
    );

    for (uint16_t index = 0; index < manager.config.network_count; index++) {
        manager.config.networks[index].priority = (uint8_t)index;
    }
}

static esp_err_t apply_ap_config_locked(void)
{
    const wifi_manager_ap_config_t *source = &manager.config.ap;
    wifi_config_t config = {0};

    copy_string((char *)config.ap.ssid, sizeof(config.ap.ssid), source->ssid);
    config.ap.ssid_len = (uint8_t)strlen(source->ssid);
    copy_string((char *)config.ap.password, sizeof(config.ap.password), source->password);
    config.ap.channel = source->channel;
    config.ap.ssid_hidden = source->hidden ? 1U : 0U;
    config.ap.max_connection = source->max_clients;
    config.ap.beacon_interval = 100U;
    config.ap.authmode = strlen(source->password) == 0U
        ? WIFI_AUTH_OPEN
        : WIFI_AUTH_WPA2_PSK;
    config.ap.pmf_cfg.capable = true;
    config.ap.pmf_cfg.required = false;

    return esp_wifi_set_config(WIFI_IF_AP, &config);
}

static void clear_connection_bits(void)
{
    if (manager.event_group != NULL) {
        xEventGroupClearBits(
            manager.event_group,
            WIFI_MANAGER_CONNECTED_BIT | WIFI_MANAGER_FAILED_BIT
        );
    }
}

static esp_err_t configure_station(const char *ssid, const char *password)
{
    wifi_config_t config = {0};

    copy_string((char *)config.sta.ssid, sizeof(config.sta.ssid), ssid);
    copy_string((char *)config.sta.password, sizeof(config.sta.password), password);

    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode = strlen(password) == 0U
        ? WIFI_AUTH_OPEN
        : WIFI_AUTH_WPA2_PSK;
    config.sta.threshold.rssi = -95;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

static void wifi_event_handler(
    void *argument,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    (void)argument;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        if (lock_manager(pdMS_TO_TICKS(20)) == ESP_OK) {
            manager.station_connecting = false;
            unlock_manager();
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        bool reconnect = false;

        if (lock_manager(pdMS_TO_TICKS(20)) == ESP_OK) {
            manager.station_connected = false;
            manager.station_connecting = false;
            manager.disconnect_count++;
            manager.last_disconnect_reason = event != NULL ? event->reason : 0U;

            reconnect = manager.started &&
                manager.reconnect_enabled &&
                !manager.manual_disconnect &&
                manager.current_ssid[0] != '\0' &&
                manager.retry_count < WIFI_MANAGER_RETRY_LIMIT;

            if (reconnect) {
                manager.retry_count++;
                manager.reconnect_count++;
                manager.station_connecting = true;
            } else if (manager.retry_count >= WIFI_MANAGER_RETRY_LIMIT && manager.event_group != NULL) {
                xEventGroupSetBits(manager.event_group, WIFI_MANAGER_FAILED_BIT);
            }

            unlock_manager();
        }

        if (reconnect) {
            ESP_LOGW(
                TAG,
                "WLAN getrennt (Grund %u), erneuter Versuch %u/%u",
                event != NULL ? event->reason : 0U,
                (unsigned int)manager.retry_count,
                WIFI_MANAGER_RETRY_LIMIT
            );
            (void)esp_wifi_connect();
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;

        if (lock_manager(pdMS_TO_TICKS(20)) == ESP_OK) {
            manager.station_connected = true;
            manager.station_connecting = false;
            manager.retry_count = 0U;
            unlock_manager();
        }

        if (manager.event_group != NULL) {
            xEventGroupSetBits(manager.event_group, WIFI_MANAGER_CONNECTED_BIT);
        }

        if (event != NULL) {
            ESP_LOGI(TAG, "WLAN verbunden, IP=" IPSTR, IP2STR(&event->ip_info.ip));
        }
    }
}

esp_err_t wifi_manager_init(void)
{
    if (manager.initialized) {
        return ESP_OK;
    }

    manager.mutex = xSemaphoreCreateMutex();
    manager.event_group = xEventGroupCreate();

    if (manager.mutex == NULL || manager.event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS konnte nicht gelöscht werden");
        result = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(result, TAG, "NVS konnte nicht initialisiert werden");

    result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }

    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }

    manager.station_netif = esp_netif_create_default_wifi_sta();
    manager.ap_netif = esp_netif_create_default_wifi_ap();

    if (manager.station_netif == NULL || manager.ap_netif == NULL) {
        return ESP_FAIL;
    }

    (void)esp_netif_set_hostname(manager.station_netif, "openaudio32");

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "WLAN-Treiber konnte nicht initialisiert werden");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "WLAN-Speichermodus konnte nicht gesetzt werden");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            wifi_event_handler,
            NULL,
            &manager.wifi_handler
        ),
        TAG,
        "WLAN-Eventhandler konnte nicht registriert werden"
    );

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            wifi_event_handler,
            NULL,
            &manager.ip_handler
        ),
        TAG,
        "IP-Eventhandler konnte nicht registriert werden"
    );

    ESP_RETURN_ON_ERROR(lock_manager(portMAX_DELAY), TAG, "Manager-Mutex konnte nicht gesperrt werden");
    result = load_config_locked();
    if (result == ESP_OK) {
        manager.initialized = true;
        manager.reconnect_enabled = true;
    }
    unlock_manager();

    return result;
}

esp_err_t wifi_manager_start(void)
{
    ESP_RETURN_ON_FALSE(manager.initialized, ESP_ERR_INVALID_STATE, TAG, "WLAN-Manager ist nicht initialisiert");

    if (manager.started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "AP+STA-Modus konnte nicht gesetzt werden");

    ESP_RETURN_ON_ERROR(lock_manager(portMAX_DELAY), TAG, "Manager-Mutex konnte nicht gesperrt werden");
    esp_err_t result = apply_ap_config_locked();
    unlock_manager();
    ESP_RETURN_ON_ERROR(result, TAG, "Access-Point-Konfiguration konnte nicht gesetzt werden");

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "WLAN konnte nicht gestartet werden");
    (void)esp_wifi_set_ps(WIFI_PS_NONE);

    manager.started = true;

    ESP_LOGI(TAG, "Setup-WLAN '%s' aktiv", manager.config.ap.ssid);

    result = wifi_manager_connect_best_saved(WIFI_MANAGER_DEFAULT_CONNECT_TIMEOUT_MS);
    if (result == ESP_ERR_NOT_FOUND || result == ESP_ERR_TIMEOUT || result == ESP_FAIL) {
        ESP_LOGW(TAG, "Kein bekanntes WLAN verbunden; Setup-Access-Point bleibt erreichbar");
        return ESP_OK;
    }

    return result;
}

esp_err_t wifi_manager_stop(void)
{
    if (!manager.started) {
        return ESP_OK;
    }

    manager.reconnect_enabled = false;
    manager.manual_disconnect = true;
    esp_err_t result = esp_wifi_stop();
    manager.started = false;
    manager.station_connected = false;
    manager.station_connecting = false;
    return result;
}

esp_err_t wifi_manager_connect(
    const char *ssid,
    const char *password,
    bool save_network,
    uint32_t timeout_ms
)
{
    if (!manager.initialized || !manager.started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!valid_ssid(ssid) || !valid_password(password)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (timeout_ms == 0U) {
        timeout_ms = WIFI_MANAGER_DEFAULT_CONNECT_TIMEOUT_MS;
    }

    clear_connection_bits();

    ESP_RETURN_ON_ERROR(lock_manager(pdMS_TO_TICKS(250)), TAG, "Manager-Mutex konnte nicht gesperrt werden");
    manager.manual_disconnect = false;
    manager.reconnect_enabled = true;
    manager.station_connecting = true;
    manager.station_connected = false;
    manager.retry_count = 0U;
    copy_string(manager.current_ssid, sizeof(manager.current_ssid), ssid);
    unlock_manager();

    (void)esp_wifi_disconnect();
    ESP_RETURN_ON_ERROR(configure_station(ssid, password), TAG, "Station konnte nicht konfiguriert werden");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "Verbindungsaufbau konnte nicht gestartet werden");

    const EventBits_t bits = xEventGroupWaitBits(
        manager.event_group,
        WIFI_MANAGER_CONNECTED_BIT | WIFI_MANAGER_FAILED_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );

    if ((bits & WIFI_MANAGER_CONNECTED_BIT) == 0U) {
        if (lock_manager(pdMS_TO_TICKS(100)) == ESP_OK) {
            manager.station_connecting = false;
            unlock_manager();
        }
        return ESP_ERR_TIMEOUT;
    }

    if (save_network) {
        wifi_manager_network_t network = {0};
        copy_string(network.ssid, sizeof(network.ssid), ssid);
        copy_string(network.password, sizeof(network.password), password);
        network.auto_connect = true;
        network.hidden = false;
        network.priority = 0U;
        ESP_RETURN_ON_ERROR(wifi_manager_save_network(&network), TAG, "Netzwerk konnte nicht gespeichert werden");
    }

    return ESP_OK;
}

esp_err_t wifi_manager_connect_best_saved(uint32_t timeout_ms)
{
    wifi_manager_network_t networks[WIFI_MANAGER_MAX_NETWORKS] = {0};
    size_t count = 0U;
    ESP_RETURN_ON_ERROR(
        wifi_manager_get_saved_networks(networks, WIFI_MANAGER_MAX_NETWORKS, &count),
        TAG,
        "Bekannte Netze konnten nicht gelesen werden"
    );

    if (count == 0U) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t last_result = ESP_ERR_NOT_FOUND;

    for (size_t index = 0; index < count; index++) {
        if (!networks[index].auto_connect) {
            continue;
        }

        ESP_LOGI(TAG, "Versuche bekanntes WLAN '%s'", networks[index].ssid);
        last_result = wifi_manager_connect(
            networks[index].ssid,
            networks[index].password,
            false,
            timeout_ms
        );

        if (last_result == ESP_OK) {
            return ESP_OK;
        }
    }

    return last_result;
}

esp_err_t wifi_manager_disconnect(void)
{
    if (!manager.started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (lock_manager(pdMS_TO_TICKS(100)) == ESP_OK) {
        manager.manual_disconnect = true;
        manager.station_connecting = false;
        manager.current_ssid[0] = '\0';
        unlock_manager();
    }

    return esp_wifi_disconnect();
}

esp_err_t wifi_manager_save_network(const wifi_manager_network_t *network)
{
    if (network == NULL || !valid_ssid(network->ssid) || !valid_password(network->password)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(lock_manager(pdMS_TO_TICKS(250)), TAG, "Manager-Mutex konnte nicht gesperrt werden");

    int index = find_network_index_locked(network->ssid);
    if (index < 0) {
        if (manager.config.network_count >= WIFI_MANAGER_MAX_NETWORKS) {
            unlock_manager();
            return ESP_ERR_NO_MEM;
        }
        index = (int)manager.config.network_count++;
    }

    manager.config.networks[index] = *network;
    manager.config.networks[index].ssid[WIFI_MANAGER_SSID_MAX_LENGTH] = '\0';
    manager.config.networks[index].password[WIFI_MANAGER_PASSWORD_MAX_LENGTH] = '\0';

    normalize_priorities_locked();
    const esp_err_t result = save_config_locked();
    unlock_manager();
    return result;
}

esp_err_t wifi_manager_forget_network(const char *ssid)
{
    if (!valid_ssid(ssid)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(lock_manager(pdMS_TO_TICKS(250)), TAG, "Manager-Mutex konnte nicht gesperrt werden");
    const int index = find_network_index_locked(ssid);
    if (index < 0) {
        unlock_manager();
        return ESP_ERR_NOT_FOUND;
    }

    for (uint16_t position = (uint16_t)index;
         position + 1U < manager.config.network_count;
         position++) {
        manager.config.networks[position] = manager.config.networks[position + 1U];
    }

    manager.config.network_count--;
    memset(
        &manager.config.networks[manager.config.network_count],
        0,
        sizeof(manager.config.networks[0])
    );
    normalize_priorities_locked();
    const esp_err_t result = save_config_locked();
    unlock_manager();
    return result;
}

esp_err_t wifi_manager_forget_all_networks(void)
{
    ESP_RETURN_ON_ERROR(lock_manager(pdMS_TO_TICKS(250)), TAG, "Manager-Mutex konnte nicht gesperrt werden");
    memset(manager.config.networks, 0, sizeof(manager.config.networks));
    manager.config.network_count = 0U;
    const esp_err_t result = save_config_locked();
    unlock_manager();
    return result;
}

esp_err_t wifi_manager_get_saved_networks(
    wifi_manager_network_t *networks,
    size_t capacity,
    size_t *network_count
)
{
    if (network_count == NULL || (capacity > 0U && networks == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(lock_manager(pdMS_TO_TICKS(100)), TAG, "Manager-Mutex konnte nicht gesperrt werden");
    const size_t count = manager.config.network_count;
    const size_t copied = count < capacity ? count : capacity;

    if (copied > 0U) {
        memcpy(networks, manager.config.networks, copied * sizeof(networks[0]));
    }
    *network_count = copied;
    unlock_manager();

    return copied < count ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

esp_err_t wifi_manager_set_network_priority(const char *ssid, uint8_t priority)
{
    if (!valid_ssid(ssid)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(lock_manager(pdMS_TO_TICKS(250)), TAG, "Manager-Mutex konnte nicht gesperrt werden");
    const int index = find_network_index_locked(ssid);
    if (index < 0) {
        unlock_manager();
        return ESP_ERR_NOT_FOUND;
    }

    if (manager.config.network_count > 0U && priority >= manager.config.network_count) {
        priority = (uint8_t)(manager.config.network_count - 1U);
    }

    manager.config.networks[index].priority = priority;
    normalize_priorities_locked();
    const esp_err_t result = save_config_locked();
    unlock_manager();
    return result;
}

esp_err_t wifi_manager_scan(
    wifi_manager_scan_result_t *results,
    size_t capacity,
    size_t *result_count
)
{
    if (!manager.started || result_count == NULL || (capacity > 0U && results == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 40,
                .max = 120,
            },
        },
        .home_chan_dwell_time = 30,
    };

    esp_err_t result = esp_wifi_scan_start(&scan_config, true);
    if (result != ESP_OK) {
        return result;
    }

    uint16_t found = 0U;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&found), TAG, "Scan-Ergebnisanzahl konnte nicht gelesen werden");

    uint16_t requested = found;
    if (requested > WIFI_MANAGER_MAX_SCAN_RESULTS) {
        requested = WIFI_MANAGER_MAX_SCAN_RESULTS;
    }
    if (requested > capacity) {
        requested = (uint16_t)capacity;
    }

    wifi_ap_record_t *records = NULL;
    if (requested > 0U) {
        records = calloc(requested, sizeof(*records));
        if (records == NULL) {
            return ESP_ERR_NO_MEM;
        }

        result = esp_wifi_scan_get_ap_records(&requested, records);
        if (result != ESP_OK) {
            free(records);
            return result;
        }
    }

    for (uint16_t index = 0; index < requested; index++) {
        wifi_manager_scan_result_t *destination = &results[index];
        memset(destination, 0, sizeof(*destination));
        copy_string(destination->ssid, sizeof(destination->ssid), (const char *)records[index].ssid);
        memcpy(destination->bssid, records[index].bssid, sizeof(destination->bssid));
        destination->rssi = records[index].rssi;
        destination->channel = records[index].primary;
        destination->authmode = records[index].authmode;

        if (lock_manager(pdMS_TO_TICKS(20)) == ESP_OK) {
            destination->known = find_network_index_locked(destination->ssid) >= 0;
            unlock_manager();
        }
    }

    free(records);
    *result_count = requested;
    return ESP_OK;
}

esp_err_t wifi_manager_get_status(wifi_manager_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));

    ESP_RETURN_ON_ERROR(lock_manager(pdMS_TO_TICKS(100)), TAG, "Manager-Mutex konnte nicht gesperrt werden");
    status->initialized = manager.initialized;
    status->started = manager.started;
    status->station_connected = manager.station_connected;
    status->station_connecting = manager.station_connecting;
    status->access_point_active = manager.started && manager.config.ap.enabled;
    status->disconnect_count = manager.disconnect_count;
    status->reconnect_count = manager.reconnect_count;
    status->last_disconnect_reason = manager.last_disconnect_reason;
    copy_string(status->station_ssid, sizeof(status->station_ssid), manager.current_ssid);
    unlock_manager();

    if (status->station_connected) {
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            copy_string(status->station_ssid, sizeof(status->station_ssid), (const char *)ap.ssid);
            memcpy(status->station_bssid, ap.bssid, sizeof(status->station_bssid));
            status->rssi = ap.rssi;
            status->channel = ap.primary;
            status->authmode = ap.authmode;
        }

        esp_netif_ip_info_t ip_info = {0};
        if (manager.station_netif != NULL &&
            esp_netif_get_ip_info(manager.station_netif, &ip_info) == ESP_OK) {
            (void)snprintf(status->ip_address, sizeof(status->ip_address), IPSTR, IP2STR(&ip_info.ip));
            (void)snprintf(status->gateway, sizeof(status->gateway), IPSTR, IP2STR(&ip_info.gw));
            (void)snprintf(status->netmask, sizeof(status->netmask), IPSTR, IP2STR(&ip_info.netmask));
        }
    }

    esp_netif_ip_info_t ap_ip = {0};
    if (manager.ap_netif != NULL && esp_netif_get_ip_info(manager.ap_netif, &ap_ip) == ESP_OK) {
        (void)snprintf(status->ap_ip_address, sizeof(status->ap_ip_address), IPSTR, IP2STR(&ap_ip.ip));
    }

    return ESP_OK;
}

esp_err_t wifi_manager_get_ap_config(wifi_manager_ap_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(lock_manager(pdMS_TO_TICKS(100)), TAG, "Manager-Mutex konnte nicht gesperrt werden");
    *config = manager.config.ap;
    unlock_manager();
    return ESP_OK;
}

esp_err_t wifi_manager_set_ap_config(const wifi_manager_ap_config_t *config)
{
    if (!valid_ap_config(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(lock_manager(pdMS_TO_TICKS(250)), TAG, "Manager-Mutex konnte nicht gesperrt werden");
    manager.config.ap = *config;
    manager.config.ap.ssid[WIFI_MANAGER_AP_SSID_MAX_LENGTH] = '\0';
    manager.config.ap.password[WIFI_MANAGER_AP_PASSWORD_MAX_LENGTH] = '\0';

    esp_err_t result = save_config_locked();
    if (result == ESP_OK && manager.started) {
        result = apply_ap_config_locked();
    }
    unlock_manager();
    return result;
}

esp_err_t wifi_manager_factory_reset(void)
{
    ESP_RETURN_ON_ERROR(lock_manager(pdMS_TO_TICKS(250)), TAG, "Manager-Mutex konnte nicht gesperrt werden");
    set_default_config(&manager.config);
    const esp_err_t result = save_config_locked();
    unlock_manager();
    return result;
}
