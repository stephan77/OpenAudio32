#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MANAGER_MAX_NETWORKS          10U
#define WIFI_MANAGER_MAX_SCAN_RESULTS      24U
#define WIFI_MANAGER_SSID_MAX_LENGTH       32U
#define WIFI_MANAGER_PASSWORD_MAX_LENGTH   64U
#define WIFI_MANAGER_HOSTNAME_MAX_LENGTH   32U
#define WIFI_MANAGER_AP_SSID_MAX_LENGTH    32U
#define WIFI_MANAGER_AP_PASSWORD_MAX_LENGTH 64U

#define WIFI_MANAGER_DEFAULT_AP_CHANNEL    1U
#define WIFI_MANAGER_DEFAULT_AP_MAX_CLIENTS 4U
#define WIFI_MANAGER_DEFAULT_CONNECT_TIMEOUT_MS 15000U

typedef struct {
    char ssid[WIFI_MANAGER_SSID_MAX_LENGTH + 1U];
    char password[WIFI_MANAGER_PASSWORD_MAX_LENGTH + 1U];
    uint8_t priority;          /* 0 = höchste Priorität */
    bool auto_connect;
    bool hidden;
} wifi_manager_network_t;

typedef struct {
    char ssid[WIFI_MANAGER_SSID_MAX_LENGTH + 1U];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    wifi_auth_mode_t authmode;
    bool known;
} wifi_manager_scan_result_t;

typedef struct {
    bool enabled;
    bool always_on;
    bool hidden;
    uint8_t channel;
    uint8_t max_clients;
    char ssid[WIFI_MANAGER_AP_SSID_MAX_LENGTH + 1U];
    char password[WIFI_MANAGER_AP_PASSWORD_MAX_LENGTH + 1U];
} wifi_manager_ap_config_t;

typedef struct {
    bool initialized;
    bool started;
    bool station_connected;
    bool station_connecting;
    bool access_point_active;
    bool internet_reachable; /* reserviert; Diagnosemodul setzt dies später */

    char station_ssid[WIFI_MANAGER_SSID_MAX_LENGTH + 1U];
    uint8_t station_bssid[6];
    int8_t rssi;
    uint8_t channel;
    wifi_auth_mode_t authmode;

    char ip_address[16];
    char gateway[16];
    char netmask[16];
    char ap_ip_address[16];

    uint32_t disconnect_count;
    uint32_t reconnect_count;
    uint32_t last_disconnect_reason;
} wifi_manager_status_t;

/** Initialisiert NVS, TCP/IP, Eventloop und WLAN-Treiber. */
esp_err_t wifi_manager_init(void);

/** Startet WLAN im AP+STA-Modus und verbindet sich mit bekannten Netzen. */
esp_err_t wifi_manager_start(void);

/** Stoppt den WLAN-Treiber. Gespeicherte Netze bleiben erhalten. */
esp_err_t wifi_manager_stop(void);

/** Liefert eine Momentaufnahme des aktuellen Zustands. */
esp_err_t wifi_manager_get_status(wifi_manager_status_t *status);

/** Führt einen synchronen Umgebungsscan aus. */
esp_err_t wifi_manager_scan(
    wifi_manager_scan_result_t *results,
    size_t capacity,
    size_t *result_count
);

/** Verbindet sich und speichert das Netz optional dauerhaft. */
esp_err_t wifi_manager_connect(
    const char *ssid,
    const char *password,
    bool save_network,
    uint32_t timeout_ms
);

/** Verbindet sich mit dem am höchsten priorisierten bekannten Netz. */
esp_err_t wifi_manager_connect_best_saved(uint32_t timeout_ms);

/** Trennt die Station. Der Konfigurations-AP bleibt aktiv. */
esp_err_t wifi_manager_disconnect(void);

/** Fügt ein Netz hinzu oder aktualisiert ein vorhandenes Netz. */
esp_err_t wifi_manager_save_network(const wifi_manager_network_t *network);

/** Entfernt ein bekanntes Netz anhand seiner SSID. */
esp_err_t wifi_manager_forget_network(const char *ssid);

/** Entfernt alle bekannten Netze. */
esp_err_t wifi_manager_forget_all_networks(void);

/** Liest gespeicherte Netze nach Priorität sortiert. */
esp_err_t wifi_manager_get_saved_networks(
    wifi_manager_network_t *networks,
    size_t capacity,
    size_t *network_count
);

/** Ändert die Priorität eines bekannten Netzes. */
esp_err_t wifi_manager_set_network_priority(
    const char *ssid,
    uint8_t priority
);

/** Liest die Konfiguration des eigenen Setup-Access-Points. */
esp_err_t wifi_manager_get_ap_config(wifi_manager_ap_config_t *config);

/** Speichert und übernimmt die Konfiguration des Setup-Access-Points. */
esp_err_t wifi_manager_set_ap_config(const wifi_manager_ap_config_t *config);

/** Setzt WLAN-Netze und AP-Einstellungen auf Werkseinstellungen zurück. */
esp_err_t wifi_manager_factory_reset(void);

#ifdef __cplusplus
}
#endif
