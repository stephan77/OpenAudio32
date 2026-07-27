#include "radio_player.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "audio.h"
#include "esp_log.h"
#include "station_manager.h"
#include "streamer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "media_manager.h"

#define RADIO_PLAYER_TASK_STACK_SIZE 20480
#define RADIO_PLAYER_TASK_PRIORITY   7
#define RADIO_PLAYER_TASK_CORE       1

static const char *TAG = "radio_player";

static TaskHandle_t radio_task_handle = NULL;
static SemaphoreHandle_t radio_mutex = NULL;

static radio_station_t active_station = {0};

static bool initialized = false;
static bool task_running = false;
static esp_err_t radio_player_media_stop(void)
{
    return radio_player_stop(
        5000U
    );
}
static void radio_player_task(void *argument)
{
    radio_station_t station =
        *(const radio_station_t *)argument;

    ESP_LOGI(
        TAG,
        "Starte Sender: %s",
        station.name
    );

    ESP_LOGI(
        TAG,
        "Stream-URL: %s",
        station.url
    );

    esp_err_t result =
        streamer_play_mp3_radio(
            station.url
        );

    if (result == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(
            TAG,
            "Radiostream wurde kontrolliert beendet"
        );
    } else if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Radiostream beendet: %s",
            esp_err_to_name(result)
        );
    }

    audio_set_playback_active(false);

    if (radio_mutex != NULL &&
        xSemaphoreTake(
            radio_mutex,
            portMAX_DELAY
        ) == pdTRUE) {

        task_running = false;
        radio_task_handle = NULL;

        xSemaphoreGive(radio_mutex);
    }

(void)media_manager_release(

    MEDIA_SOURCE_RADIO

);

ESP_LOGI(

    TAG,

    "Radio-Task beendet"

);

vTaskDelete(NULL);
}

esp_err_t radio_player_init(void)

{

    if (initialized) {

        return ESP_OK;

    }

    radio_mutex =

        xSemaphoreCreateMutex();

    if (radio_mutex == NULL) {

        return ESP_ERR_NO_MEM;

    }

    esp_err_t result =

        media_manager_register_source(

            MEDIA_SOURCE_RADIO,

            radio_player_media_stop

        );

    if (result != ESP_OK) {

        vSemaphoreDelete(

            radio_mutex

        );

        radio_mutex = NULL;

        return result;

    }

    streamer_radio_request_stop();

    memset(

        &active_station,

        0,

        sizeof(active_station)

    );

    initialized = true;

    ESP_LOGI(

        TAG,

        "Radio Player initialisiert"

    );

    return ESP_OK;

}

bool radio_player_is_running(void)
{
    if (!initialized ||
        radio_mutex == NULL) {

        return false;
    }

    bool running = false;

    if (xSemaphoreTake(
            radio_mutex,
            pdMS_TO_TICKS(100)
        ) == pdTRUE) {

        running = task_running;

        xSemaphoreGive(radio_mutex);
    }

    return running;
}

uint32_t radio_player_get_station_id(void)
{
    if (!initialized ||
        radio_mutex == NULL) {

        return 0;
    }

    uint32_t station_id = 0;

    if (xSemaphoreTake(
            radio_mutex,
            pdMS_TO_TICKS(100)
        ) == pdTRUE) {

        station_id =
            active_station.id;

        xSemaphoreGive(radio_mutex);
    }

    return station_id;
}

esp_err_t radio_player_stop(uint32_t timeout_ms)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!radio_player_is_running()) {
        return ESP_OK;
    }

    ESP_LOGI(
        TAG,
        "Stoppe laufenden Radiostream"
    );

    streamer_radio_request_stop();

    const TickType_t start_tick =
        xTaskGetTickCount();

    const TickType_t timeout_ticks =
        pdMS_TO_TICKS(timeout_ms);

    while (radio_player_is_running()) {
        if ((xTaskGetTickCount() - start_tick) >=
            timeout_ticks) {

            ESP_LOGE(
                TAG,
                "Radio-Task wurde nicht rechtzeitig beendet"
            );

            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(
            pdMS_TO_TICKS(25)
        );
    }

    return ESP_OK;
}

esp_err_t radio_player_play_station(
    uint32_t station_id
)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    radio_station_t station = {0};

    esp_err_t result =
        station_manager_get_by_id(
            station_id,
            &station
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        radio_player_stop(
            3000U
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        media_manager_activate(
            MEDIA_SOURCE_RADIO
        );

    if (result != ESP_OK) {
        return result;
    }

    streamer_radio_clear_stop();

    if (xSemaphoreTake(
            radio_mutex,
            pdMS_TO_TICKS(500)
        ) != pdTRUE) {

        (void)media_manager_release(
            MEDIA_SOURCE_RADIO
        );

        return ESP_ERR_TIMEOUT;
    }

    active_station =
        station;

    task_running =
        true;

    ESP_LOGI(
        TAG,
        "Free internal: %u",
        (unsigned)heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL
        )
    );

    ESP_LOGI(
        TAG,
        "Largest block: %u",
        (unsigned)heap_caps_get_largest_free_block(
            MALLOC_CAP_INTERNAL
        )
    );

    BaseType_t task_result =
        xTaskCreatePinnedToCore(
            radio_player_task,
            "radio_stream",
            RADIO_PLAYER_TASK_STACK_SIZE,
            &active_station,
            RADIO_PLAYER_TASK_PRIORITY,
            &radio_task_handle,
            RADIO_PLAYER_TASK_CORE
        );

    if (task_result != pdPASS) {
        task_running =
            false;

        radio_task_handle =
            NULL;

        xSemaphoreGive(
            radio_mutex
        );

        (void)media_manager_release(
            MEDIA_SOURCE_RADIO
        );

        return ESP_ERR_NO_MEM;
    }

    xSemaphoreGive(
        radio_mutex
    );

    result =
        station_manager_set_current(
            station_id
        );

    if (result != ESP_OK) {
        streamer_radio_request_stop();

        ESP_LOGE(
            TAG,
            "Aktueller Sender konnte nicht gespeichert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    return ESP_OK;
}

esp_err_t radio_player_play_current(void)
{
    radio_station_t station = {0};

    esp_err_t result =
        station_manager_get_current(
            &station
        );

    if (result != ESP_OK) {
        return result;
    }

    return radio_player_play_station(
        station.id
    );
}