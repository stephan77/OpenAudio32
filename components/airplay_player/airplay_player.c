#include "airplay_player.h"
#include "airplay_core.h"

#include "esp_log.h"
#include "media_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG =
    "airplay_player";

typedef struct {
    bool initialized;
    bool started;
    bool connected;

    airplay_player_state_t state;

    SemaphoreHandle_t mutex;
} airplay_player_context_t;

static airplay_player_context_t player = {
    .initialized = false,
    .started = false,
    .connected = false,
    .state = AIRPLAY_PLAYER_STATE_UNINITIALIZED,
    .mutex = NULL,
};

static esp_err_t lock_player(
    TickType_t timeout
)
{
    if (player.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            player.mutex,
            timeout
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void unlock_player(void)
{
    if (player.mutex != NULL) {
        xSemaphoreGive(
            player.mutex
        );
    }
}

const char *airplay_player_state_name(
    airplay_player_state_t state
)
{
    switch (state) {
    case AIRPLAY_PLAYER_STATE_STOPPED:
        return "Gestoppt";

    case AIRPLAY_PLAYER_STATE_READY:
        return "Bereit";

    case AIRPLAY_PLAYER_STATE_CONNECTED:
        return "Verbunden";

    case AIRPLAY_PLAYER_STATE_PLAYING:
        return "Wiedergabe";

    case AIRPLAY_PLAYER_STATE_PAUSED:
        return "Pausiert";

    case AIRPLAY_PLAYER_STATE_ERROR:
        return "Fehler";

    case AIRPLAY_PLAYER_STATE_UNINITIALIZED:
    default:
        return "Nicht initialisiert";
    }
}

esp_err_t airplay_player_init(void)
{
    if (player.initialized) {
        return ESP_OK;
    }

    player.mutex =
        xSemaphoreCreateMutex();

    if (player.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
esp_err_t result =
    airplay_core_init();

if (result != ESP_OK) {
    vSemaphoreDelete(
        player.mutex
    );

    player.mutex =
        NULL;

    return result;
}
    /*
     * AirPlay wird später als dauerhafter Netzwerkdienst
     * gestartet. Eine aktive Wiedergabesitzung übernimmt
     * dann MEDIA_SOURCE_AIRPLAY.
     */
    player.initialized =
        true;

    player.started =
        false;

    player.connected =
        false;

    player.state =
        AIRPLAY_PLAYER_STATE_STOPPED;

    ESP_LOGI(
        TAG,
        "AirPlay-Player initialisiert"
    );

    return ESP_OK;
}

esp_err_t airplay_player_start(void)
{
    if (!player.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result =
        lock_player(
            pdMS_TO_TICKS(500)
        );

    if (result != ESP_OK) {
        return result;
    }

    if (player.started) {
        unlock_player();
        return ESP_OK;
    }

result =
    airplay_core_start();

if (result != ESP_OK) {
    player.state =
        AIRPLAY_PLAYER_STATE_ERROR;

    unlock_player();

    return result;
}

    player.started =
        true;

    player.connected =
        false;

    player.state =
        AIRPLAY_PLAYER_STATE_READY;

    unlock_player();

    ESP_LOGI(
        TAG,
        "AirPlay-Dienst vorbereitet"
    );

    return ESP_OK;
}

esp_err_t airplay_player_stop(void)
{
    if (!player.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result =
        lock_player(
            pdMS_TO_TICKS(500)
        );

    if (result != ESP_OK) {
        return result;
    }

    if (!player.started) {
        unlock_player();
        return ESP_OK;
    }

result =
    airplay_core_stop();

if (result != ESP_OK) {
    player.state =
        AIRPLAY_PLAYER_STATE_ERROR;

    unlock_player();

    return result;
}

    player.started =
        false;

    player.connected =
        false;

    player.state =
        AIRPLAY_PLAYER_STATE_STOPPED;

    unlock_player();

    (void)media_manager_release(
        MEDIA_SOURCE_AIRPLAY
    );

    ESP_LOGI(
        TAG,
        "AirPlay-Dienst gestoppt"
    );

    return ESP_OK;
}

bool airplay_player_is_started(void)
{
    if (!player.initialized) {
        return false;
    }

    bool started =
        false;

    if (lock_player(
            pdMS_TO_TICKS(100)
        ) == ESP_OK) {

        started =
            player.started;

        unlock_player();
    }

    return started;
}

bool airplay_player_is_connected(void)
{
    if (!player.initialized) {
        return false;
    }

    bool connected =
        false;

    if (lock_player(
            pdMS_TO_TICKS(100)
        ) == ESP_OK) {

        connected =
            player.connected;

        unlock_player();
    }

    return connected;
}

airplay_player_state_t airplay_player_get_state(void)
{
    if (!player.initialized) {
        return AIRPLAY_PLAYER_STATE_UNINITIALIZED;
    }

    airplay_player_state_t state =
        AIRPLAY_PLAYER_STATE_ERROR;

    if (lock_player(
            pdMS_TO_TICKS(100)
        ) == ESP_OK) {

        state =
            player.state;

        unlock_player();
    }

    return state;
}