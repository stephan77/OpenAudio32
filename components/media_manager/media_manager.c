#include "media_manager.h"

#include <stddef.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "media_manager";

typedef struct {
    bool initialized;

    SemaphoreHandle_t mutex;

    media_source_t active_source;

    media_stop_callback_t stop_callbacks[
        MEDIA_SOURCE_AIRPLAY + 1
    ];
} media_manager_context_t;

static media_manager_context_t manager = {
    .initialized = false,
    .mutex = NULL,
    .active_source = MEDIA_SOURCE_NONE,
    .stop_callbacks = {0},
};

static bool source_is_valid(
    media_source_t source
)
{
    return source >= MEDIA_SOURCE_NONE &&
           source <= MEDIA_SOURCE_AIRPLAY;
}

const char *media_manager_source_name(
    media_source_t source
)
{
    switch (source) {
    case MEDIA_SOURCE_RADIO:
        return "Radio";

    case MEDIA_SOURCE_SPOTIFY:
        return "Spotify";

    case MEDIA_SOURCE_AIRPLAY:
        return "AirPlay";

    case MEDIA_SOURCE_NONE:
    default:
        return "Keine";
    }
}

esp_err_t media_manager_init(void)
{
    if (manager.initialized) {
        return ESP_OK;
    }

    manager.mutex =
        xSemaphoreCreateMutex();

    if (manager.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    manager.active_source =
        MEDIA_SOURCE_NONE;

    manager.initialized = true;

    ESP_LOGI(
        TAG,
        "Medienverwaltung initialisiert"
    );

    return ESP_OK;
}

esp_err_t media_manager_register_source(
    media_source_t source,
    media_stop_callback_t stop_callback
)
{
    if (!manager.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!source_is_valid(source) ||
        source == MEDIA_SOURCE_NONE) {

        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(
            manager.mutex,
            pdMS_TO_TICKS(500)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    manager.stop_callbacks[source] =
        stop_callback;

    xSemaphoreGive(
        manager.mutex
    );

    return ESP_OK;
}

esp_err_t media_manager_activate(
    media_source_t source
)
{
    if (!manager.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!source_is_valid(source) ||
        source == MEDIA_SOURCE_NONE) {

        return ESP_ERR_INVALID_ARG;
    }

    media_source_t previous_source =
        MEDIA_SOURCE_NONE;

    media_stop_callback_t previous_stop =
        NULL;

    if (xSemaphoreTake(
            manager.mutex,
            pdMS_TO_TICKS(500)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    if (manager.active_source == source) {
        xSemaphoreGive(
            manager.mutex
        );

        return ESP_OK;
    }

    previous_source =
        manager.active_source;

    if (source_is_valid(previous_source)) {
        previous_stop =
            manager.stop_callbacks[
                previous_source
            ];
    }

    /*
     * Während die alte Quelle gestoppt wird, gilt
     * zunächst keine Quelle als aktiv.
     */
    manager.active_source =
        MEDIA_SOURCE_NONE;

    xSemaphoreGive(
        manager.mutex
    );

    if (previous_source != MEDIA_SOURCE_NONE &&
        previous_stop != NULL) {

        ESP_LOGI(
            TAG,
            "Stoppe Quelle %s vor Wechsel zu %s",
            media_manager_source_name(
                previous_source
            ),
            media_manager_source_name(
                source
            )
        );

        esp_err_t stop_result =
            previous_stop();

        if (stop_result != ESP_OK &&
            stop_result != ESP_ERR_INVALID_STATE) {

            ESP_LOGE(
                TAG,
                "Quelle %s konnte nicht beendet werden: %s",
                media_manager_source_name(
                    previous_source
                ),
                esp_err_to_name(
                    stop_result
                )
            );

            return stop_result;
        }
    }

    if (xSemaphoreTake(
            manager.mutex,
            pdMS_TO_TICKS(500)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    manager.active_source =
        source;

    xSemaphoreGive(
        manager.mutex
    );

    ESP_LOGI(
        TAG,
        "Aktive Quelle: %s",
        media_manager_source_name(
            source
        )
    );

    return ESP_OK;
}

esp_err_t media_manager_release(
    media_source_t source
)
{
    if (!manager.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!source_is_valid(source) ||
        source == MEDIA_SOURCE_NONE) {

        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(
            manager.mutex,
            pdMS_TO_TICKS(500)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    if (manager.active_source == source) {
        manager.active_source =
            MEDIA_SOURCE_NONE;
    }

    xSemaphoreGive(
        manager.mutex
    );

    return ESP_OK;
}

media_source_t media_manager_get_active_source(void)
{
    if (!manager.initialized ||
        manager.mutex == NULL) {

        return MEDIA_SOURCE_NONE;
    }

    media_source_t source =
        MEDIA_SOURCE_NONE;

    if (xSemaphoreTake(
            manager.mutex,
            pdMS_TO_TICKS(100)
        ) == pdTRUE) {

        source =
            manager.active_source;

        xSemaphoreGive(
            manager.mutex
        );
    }

    return source;
}