#include "station_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define STATION_NVS_NAMESPACE       "stations"
#define STATION_NVS_LIST_KEY        "station_list"
#define STATION_NVS_CURRENT_KEY     "current_id"

#define STATION_STORAGE_VERSION     1U

static const char *TAG = "stations";

typedef struct {
    uint32_t version;
    uint32_t count;
    uint32_t next_id;

    radio_station_t stations[STATION_MANAGER_MAX_STATIONS];
} station_storage_t;

static station_storage_t storage;
static uint32_t current_station_id = 0;
static bool initialized = false;

static bool station_name_valid(const char *name)
{
    if (name == NULL) {
        return false;
    }

    const size_t length =
        strnlen(name, STATION_NAME_MAX_LENGTH);

    return length > 0 &&
           length < STATION_NAME_MAX_LENGTH;
}

static bool station_url_valid(const char *url)
{
    if (url == NULL) {
        return false;
    }

    const size_t length =
        strnlen(url, STATION_URL_MAX_LENGTH);

    if (length == 0 ||
        length >= STATION_URL_MAX_LENGTH) {

        return false;
    }

    return strncmp(url, "http://", 7) == 0 ||
           strncmp(url, "https://", 8) == 0;
}

static int find_station_index(uint32_t id)
{
    for (size_t index = 0;
         index < storage.count;
         index++) {

        if (storage.stations[index].id == id) {
            return (int)index;
        }
    }

    return -1;
}

static void create_default_station(void)
{
    memset(&storage, 0, sizeof(storage));

    storage.version = STATION_STORAGE_VERSION;
    storage.count = 1;
    storage.next_id = 2;

    radio_station_t *station =
        &storage.stations[0];

    station->id = 1;

    snprintf(
        station->name,
        sizeof(station->name),
        "%s",
        "Radio Salü"
    );

    snprintf(
        station->url,
        sizeof(station->url),
        "%s",
        "http://internetradio.salue.de:8000/channel1"
    );

    station->favorite = true;

    current_station_id = station->id;

    ESP_LOGI(TAG, "Standardsender Radio Salü angelegt");
}

static esp_err_t load_from_nvs(void)
{
    nvs_handle_t handle;

    esp_err_t result = nvs_open(
        STATION_NVS_NAMESPACE,
        NVS_READONLY,
        &handle
    );

    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }

    if (result != ESP_OK) {
        return result;
    }

    size_t blob_size = sizeof(storage);

    result = nvs_get_blob(
        handle,
        STATION_NVS_LIST_KEY,
        &storage,
        &blob_size
    );

    if (result != ESP_OK) {
        nvs_close(handle);
        return result;
    }

    result = nvs_get_u32(
        handle,
        STATION_NVS_CURRENT_KEY,
        &current_station_id
    );

    nvs_close(handle);

    if (result == ESP_ERR_NVS_NOT_FOUND) {
        current_station_id =
            storage.count > 0
                ? storage.stations[0].id
                : 0;

        result = ESP_OK;
    }

    if (result != ESP_OK) {
        return result;
    }

    if (blob_size != sizeof(storage)) {
        ESP_LOGW(
            TAG,
            "Gespeicherte Senderstruktur hat falsche Größe"
        );

        return ESP_ERR_INVALID_SIZE;
    }

    if (storage.version != STATION_STORAGE_VERSION) {
        ESP_LOGW(
            TAG,
            "Senderstruktur-Version nicht kompatibel"
        );

        return ESP_ERR_INVALID_VERSION;
    }

    if (storage.count > STATION_MANAGER_MAX_STATIONS) {
        ESP_LOGE(
            TAG,
            "Ungültige Senderanzahl im NVS: %u",
            (unsigned int)storage.count
        );

        return ESP_ERR_INVALID_SIZE;
    }

    if (storage.count > 0 &&
        find_station_index(current_station_id) < 0) {

        current_station_id =
            storage.stations[0].id;
    }

    return ESP_OK;
}

esp_err_t station_manager_save(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;

    esp_err_t result = nvs_open(
        STATION_NVS_NAMESPACE,
        NVS_READWRITE,
        &handle
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "NVS konnte nicht geöffnet werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result = nvs_set_blob(
        handle,
        STATION_NVS_LIST_KEY,
        &storage,
        sizeof(storage)
    );

    if (result == ESP_OK) {
        result = nvs_set_u32(
            handle,
            STATION_NVS_CURRENT_KEY,
            current_station_id
        );
    }

    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }

    nvs_close(handle);

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Sender konnten nicht gespeichert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "%u Sender im NVS gespeichert",
        (unsigned int)storage.count
    );

    return ESP_OK;
}

esp_err_t station_manager_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    memset(&storage, 0, sizeof(storage));

    esp_err_t result = load_from_nvs();

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Keine gültige Senderliste gefunden: %s",
            esp_err_to_name(result)
        );

        create_default_station();

        initialized = true;

        result = station_manager_save();

        if (result != ESP_OK) {
            initialized = false;
            return result;
        }
    } else {
        initialized = true;

        ESP_LOGI(
            TAG,
            "%u Sender aus NVS geladen",
            (unsigned int)storage.count
        );
    }

    radio_station_t current;

    if (station_manager_get_current(&current) == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Aktueller Sender: %s",
            current.name
        );
    }

    return ESP_OK;
}

size_t station_manager_get_count(void)
{
    return initialized
        ? storage.count
        : 0;
}

esp_err_t station_manager_get(
    size_t index,
    radio_station_t *station
)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (station == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (index >= storage.count) {
        return ESP_ERR_NOT_FOUND;
    }

    *station = storage.stations[index];

    return ESP_OK;
}

esp_err_t station_manager_get_by_id(
    uint32_t id,
    radio_station_t *station
)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (station == NULL || id == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int index = find_station_index(id);

    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    *station = storage.stations[index];

    return ESP_OK;
}

esp_err_t station_manager_add(
    radio_station_t *station
)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (station == NULL ||
        !station_name_valid(station->name) ||
        !station_url_valid(station->url)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (storage.count >= STATION_MANAGER_MAX_STATIONS) {
        return ESP_ERR_NO_MEM;
    }

    radio_station_t new_station = {0};

    new_station.id = storage.next_id++;

    snprintf(
        new_station.name,
        sizeof(new_station.name),
        "%s",
        station->name
    );

    snprintf(
        new_station.url,
        sizeof(new_station.url),
        "%s",
        station->url
    );

    new_station.favorite = station->favorite;

    storage.stations[storage.count] =
        new_station;

    storage.count++;

    *station = new_station;

    esp_err_t result = station_manager_save();

    if (result != ESP_OK) {
        storage.count--;
        storage.next_id--;
        memset(
            &storage.stations[storage.count],
            0,
            sizeof(radio_station_t)
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "Sender hinzugefügt: %s, ID=%u",
        station->name,
        (unsigned int)station->id
    );

    return ESP_OK;
}

esp_err_t station_manager_update(
    const radio_station_t *station
)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (station == NULL ||
        station->id == 0 ||
        !station_name_valid(station->name) ||
        !station_url_valid(station->url)) {

        return ESP_ERR_INVALID_ARG;
    }

    const int index =
        find_station_index(station->id);

    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const radio_station_t old_station =
        storage.stations[index];

    radio_station_t updated_station = {0};

    updated_station.id = station->id;

    snprintf(
        updated_station.name,
        sizeof(updated_station.name),
        "%s",
        station->name
    );

    snprintf(
        updated_station.url,
        sizeof(updated_station.url),
        "%s",
        station->url
    );

    updated_station.favorite =
        station->favorite;

    storage.stations[index] =
        updated_station;

    esp_err_t result =
        station_manager_save();

    if (result != ESP_OK) {
        storage.stations[index] =
            old_station;

        return result;
    }

    ESP_LOGI(
        TAG,
        "Sender aktualisiert: %s",
        station->name
    );

    return ESP_OK;
}

esp_err_t station_manager_delete(uint32_t id)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (id == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int index =
        find_station_index(id);

    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    if (storage.count <= 1) {
        ESP_LOGW(
            TAG,
            "Der letzte Sender kann nicht gelöscht werden"
        );

        return ESP_ERR_INVALID_STATE;
    }

    const station_storage_t old_storage =
        storage;

    const uint32_t old_current_id =
        current_station_id;

    for (size_t move_index = (size_t)index;
         move_index + 1 < storage.count;
         move_index++) {

        storage.stations[move_index] =
            storage.stations[move_index + 1];
    }

    storage.count--;

    memset(
        &storage.stations[storage.count],
        0,
        sizeof(radio_station_t)
    );

    if (current_station_id == id) {
        current_station_id =
            storage.stations[0].id;
    }

    esp_err_t result =
        station_manager_save();

    if (result != ESP_OK) {
        storage = old_storage;
        current_station_id = old_current_id;

        return result;
    }

    ESP_LOGI(
        TAG,
        "Sender mit ID %u gelöscht",
        (unsigned int)id
    );

    return ESP_OK;
}

esp_err_t station_manager_get_current(
    radio_station_t *station
)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (station == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return station_manager_get_by_id(
        current_station_id,
        station
    );
}

esp_err_t station_manager_set_current(uint32_t id)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (find_station_index(id) < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const uint32_t previous_id =
        current_station_id;

    current_station_id = id;

    esp_err_t result =
        station_manager_save();

    if (result != ESP_OK) {
        current_station_id = previous_id;
        return result;
    }

    ESP_LOGI(
        TAG,
        "Aktueller Sender auf ID %u gesetzt",
        (unsigned int)id
    );

    return ESP_OK;
}

uint32_t station_manager_get_current_id(void)
{
    return initialized
        ? current_station_id
        : 0;
}