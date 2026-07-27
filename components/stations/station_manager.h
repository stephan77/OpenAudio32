#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STATION_MANAGER_MAX_STATIONS 20
#define STATION_NAME_MAX_LENGTH      48
#define STATION_URL_MAX_LENGTH       256

typedef struct {
    uint32_t id;

    char name[STATION_NAME_MAX_LENGTH];
    char url[STATION_URL_MAX_LENGTH];

    bool favorite;
} radio_station_t;

/**
 * Initialisiert den Station Manager und lädt die Sender aus NVS.
 *
 * Falls noch keine Sender gespeichert wurden, wird Radio Salü
 * als Standardsender angelegt.
 */
esp_err_t station_manager_init(void);

/**
 * Liefert die aktuelle Anzahl gespeicherter Sender.
 */
size_t station_manager_get_count(void);

/**
 * Kopiert den Sender am angegebenen Index in station.
 */
esp_err_t station_manager_get(
    size_t index,
    radio_station_t *station
);

/**
 * Sucht einen Sender anhand seiner ID.
 */
esp_err_t station_manager_get_by_id(
    uint32_t id,
    radio_station_t *station
);

/**
 * Fügt einen neuen Sender hinzu.
 *
 * Die ID wird automatisch erzeugt und in station->id geschrieben.
 */
esp_err_t station_manager_add(
    radio_station_t *station
);

/**
 * Aktualisiert einen vorhandenen Sender anhand seiner ID.
 */
esp_err_t station_manager_update(
    const radio_station_t *station
);

/**
 * Löscht einen Sender anhand seiner ID.
 */
esp_err_t station_manager_delete(uint32_t id);

/**
 * Liefert den aktuell ausgewählten Sender.
 */
esp_err_t station_manager_get_current(
    radio_station_t *station
);

/**
 * Setzt den aktuell ausgewählten Sender anhand seiner ID.
 */
esp_err_t station_manager_set_current(uint32_t id);

/**
 * Liefert die ID des aktuell ausgewählten Senders.
 */
uint32_t station_manager_get_current_id(void);
esp_err_t station_manager_update(
    const radio_station_t *station
);

esp_err_t station_manager_delete(
    uint32_t station_id
);
/**
 * Speichert Senderliste und Auswahl sofort im NVS.
 */
esp_err_t station_manager_save(void);

#ifdef __cplusplus
}
#endif