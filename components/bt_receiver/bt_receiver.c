#include "bt_receiver.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"


#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "bt_receiver";

/*
 * Verbindung zum ESP32-WROOM:
 *
 * WROOM GPIO17 TX -> S3 GPIO17 RX
 * WROOM GPIO16 RX <- S3 GPIO18 TX
 */
#define BT_RECEIVER_UART_PORT        UART_NUM_2
#define BT_RECEIVER_UART_RX_GPIO     GPIO_NUM_17
#define BT_RECEIVER_UART_TX_GPIO     GPIO_NUM_18
#define BT_RECEIVER_UART_BAUD_RATE   115200

#define BT_RECEIVER_RX_BUFFER_SIZE   2048
#define BT_RECEIVER_LINE_SIZE        320
#define BT_RECEIVER_TASK_STACK_SIZE  4096
#define BT_RECEIVER_TASK_PRIORITY    8

static bool s_initialized = false;

static SemaphoreHandle_t s_status_mutex = NULL;

static bt_receiver_status_t s_status = {
    .state = BT_RECEIVER_STATE_OFFLINE,
    .module_ready = false,
    .connected = false,
    .streaming = false,
    .volume_percent = 0,
    .play_status = 0,
    .title = "",
    .artist = "",
    .album = "",
};

static void copy_string(
    char *destination,
    size_t destination_size,
    const char *source
)
{
    if (destination == NULL ||
        destination_size == 0U) {

        return;
    }

    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    snprintf(
        destination,
        destination_size,
        "%s",
        source
    );
}

static void reset_metadata_locked(void)
{
    s_status.title[0] = '\0';
    s_status.artist[0] = '\0';
    s_status.album[0] = '\0';
}

static void process_message(
    const char *message
)
{
    if (message == NULL ||
        message[0] == '\0') {

        return;
    }

    ESP_LOGI(
        TAG,
        "Vom WROOM empfangen: %s",
        message
    );

    if (xSemaphoreTake(
            s_status_mutex,
            pdMS_TO_TICKS(100)
        ) != pdTRUE) {

        ESP_LOGW(
            TAG,
            "Status-Mutex konnte nicht übernommen werden"
        );

        return;
    }

    if (strcmp(message, "BT_BOOT") == 0) {
        s_status.state =
            BT_RECEIVER_STATE_OFFLINE;

        s_status.module_ready = false;
        s_status.connected = false;
        s_status.streaming = false;
        s_status.play_status = 0;

        reset_metadata_locked();
    } else if (strcmp(message, "BT_READY") == 0) {
        s_status.state =
            BT_RECEIVER_STATE_READY;

        s_status.module_ready = true;
        s_status.connected = false;
        s_status.streaming = false;
    } else if (strcmp(message, "BT_CONNECTED") == 0) {
        s_status.state =
            BT_RECEIVER_STATE_CONNECTED;

        s_status.module_ready = true;
        s_status.connected = true;
        s_status.streaming = false;
    } else if (strcmp(message, "BT_STREAMING") == 0) {
        s_status.state =
            BT_RECEIVER_STATE_STREAMING;

        s_status.module_ready = true;
        s_status.connected = true;
        s_status.streaming = true;
    } else if (strcmp(message, "BT_PAUSED") == 0) {
        s_status.state =
            BT_RECEIVER_STATE_PAUSED;

        s_status.streaming = false;
    } else if (strcmp(message, "BT_DISCONNECTED") == 0) {
        s_status.state =
            BT_RECEIVER_STATE_READY;

        s_status.connected = false;
        s_status.streaming = false;
        s_status.play_status = 0;

        reset_metadata_locked();
    } else if (strcmp(message, "BT_ERROR") == 0) {
        s_status.state =
            BT_RECEIVER_STATE_ERROR;

        s_status.module_ready = false;
        s_status.connected = false;
        s_status.streaming = false;
    } else if (strncmp(
                   message,
                   "BT_VOLUME:",
                   strlen("BT_VOLUME:")
               ) == 0) {

        const char *value_text =
            message + strlen("BT_VOLUME:");

        char *end = NULL;

        const unsigned long value =
            strtoul(
                value_text,
                &end,
                10
            );

        if (end != value_text &&
            *end == '\0' &&
            value <= 100UL) {

            s_status.volume_percent =
                (uint8_t)value;
        }
    } else if (strncmp(
                   message,
                   "BT_PLAY_STATUS:",
                   strlen("BT_PLAY_STATUS:")
               ) == 0) {

        const char *value_text =
            message + strlen("BT_PLAY_STATUS:");

        char *end = NULL;

        const unsigned long value =
            strtoul(
                value_text,
                &end,
                10
            );

        if (end != value_text &&
            *end == '\0' &&
            value <= UINT8_MAX) {

            s_status.play_status =
                (uint8_t)value;
        }
    } else if (strncmp(
                   message,
                   "BT_TITLE:",
                   strlen("BT_TITLE:")
               ) == 0) {

        copy_string(
            s_status.title,
            sizeof(s_status.title),
            message + strlen("BT_TITLE:")
        );
    } else if (strncmp(
                   message,
                   "BT_ARTIST:",
                   strlen("BT_ARTIST:")
               ) == 0) {

        copy_string(
            s_status.artist,
            sizeof(s_status.artist),
            message + strlen("BT_ARTIST:")
        );
    } else if (strncmp(
                   message,
                   "BT_ALBUM:",
                   strlen("BT_ALBUM:")
               ) == 0) {

        copy_string(
            s_status.album,
            sizeof(s_status.album),
            message + strlen("BT_ALBUM:")
        );
    } else if (strcmp(
                   message,
                   "BT_TRACK_CHANGED"
               ) == 0) {

        /*
         * Die neuen Metadaten folgen direkt danach.
         * Deshalb hier nicht zwingend löschen.
         */
    } else {
        ESP_LOGW(
            TAG,
            "Unbekannte WROOM-Nachricht: %s",
            message
        );
    }

    xSemaphoreGive(
        s_status_mutex
    );
}

static void bt_receiver_task(
    void *parameter
)
{
    (void)parameter;

    uint8_t received_data[128];

    char line_buffer[
        BT_RECEIVER_LINE_SIZE
    ];

    size_t line_length = 0;

    ESP_LOGI(
        TAG,
        "UART-Empfangstask gestartet"
    );

    while (true) {
        const int received_length =
            uart_read_bytes(
                BT_RECEIVER_UART_PORT,
                received_data,
                sizeof(received_data),
                pdMS_TO_TICKS(100)
            );

        if (received_length < 0) {
            ESP_LOGE(
                TAG,
                "UART-Lesefehler"
            );

            vTaskDelay(
                pdMS_TO_TICKS(100)
            );

            continue;
        }

        for (int index = 0;
             index < received_length;
             index++) {

            const char character =
                (char)received_data[index];

            if (character == '\r') {
                continue;
            }

            if (character == '\n') {
                line_buffer[line_length] =
                    '\0';

                if (line_length > 0U) {
                    process_message(
                        line_buffer
                    );
                }

                line_length = 0;
                continue;
            }

            if (line_length + 1U <
                sizeof(line_buffer)) {

                line_buffer[line_length++] =
                    character;
            } else {
                ESP_LOGW(
                    TAG,
                    "UART-Zeile zu lang, Inhalt wird verworfen"
                );

                line_length = 0;
            }
        }
    }
}

esp_err_t bt_receiver_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_status_mutex =
        xSemaphoreCreateMutex();

    if (s_status_mutex == NULL) {
        ESP_LOGE(
            TAG,
            "Status-Mutex konnte nicht erstellt werden"
        );

        return ESP_ERR_NO_MEM;
    }

    const uart_config_t uart_config = {
        .baud_rate =
            BT_RECEIVER_UART_BAUD_RATE,

        .data_bits =
            UART_DATA_8_BITS,

        .parity =
            UART_PARITY_DISABLE,

        .stop_bits =
            UART_STOP_BITS_1,

        .flow_ctrl =
            UART_HW_FLOWCTRL_DISABLE,

        .source_clk =
            UART_SCLK_DEFAULT,
    };

    esp_err_t result =
        uart_driver_install(
            BT_RECEIVER_UART_PORT,
            BT_RECEIVER_RX_BUFFER_SIZE,
            0,
            0,
            NULL,
            0
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "UART-Treiber konnte nicht installiert werden: %s",
            esp_err_to_name(result)
        );

        vSemaphoreDelete(
            s_status_mutex
        );

        s_status_mutex = NULL;

        return result;
    }

    result =
        uart_param_config(
            BT_RECEIVER_UART_PORT,
            &uart_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "UART-Konfiguration fehlgeschlagen: %s",
            esp_err_to_name(result)
        );

        uart_driver_delete(
            BT_RECEIVER_UART_PORT
        );

        vSemaphoreDelete(
            s_status_mutex
        );

        s_status_mutex = NULL;

        return result;
    }

    result =
        uart_set_pin(
            BT_RECEIVER_UART_PORT,
            BT_RECEIVER_UART_TX_GPIO,
            BT_RECEIVER_UART_RX_GPIO,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "UART-Pins konnten nicht gesetzt werden: %s",
            esp_err_to_name(result)
        );

        uart_driver_delete(
            BT_RECEIVER_UART_PORT
        );

        vSemaphoreDelete(
            s_status_mutex
        );

        s_status_mutex = NULL;

        return result;
    }

    const BaseType_t task_result =
        xTaskCreate(
            bt_receiver_task,
            "bt_receiver",
            BT_RECEIVER_TASK_STACK_SIZE,
            NULL,
            BT_RECEIVER_TASK_PRIORITY,
            NULL
        );

    if (task_result != pdPASS) {
        ESP_LOGE(
            TAG,
            "UART-Empfangstask konnte nicht gestartet werden"
        );

        uart_driver_delete(
            BT_RECEIVER_UART_PORT
        );

        vSemaphoreDelete(
            s_status_mutex
        );

        s_status_mutex = NULL;

        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;

    ESP_LOGI(
        TAG,
        "UART zum WROOM bereit: UART%d, RX=%d, TX=%d, %d Baud",
        BT_RECEIVER_UART_PORT,
        BT_RECEIVER_UART_RX_GPIO,
        BT_RECEIVER_UART_TX_GPIO,
        BT_RECEIVER_UART_BAUD_RATE
    );

    return ESP_OK;
}

esp_err_t bt_receiver_get_status(
    bt_receiver_status_t *status
)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized ||
        s_status_mutex == NULL) {

        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_status_mutex,
            pdMS_TO_TICKS(100)
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    *status =
        s_status;

    xSemaphoreGive(
        s_status_mutex
    );

    return ESP_OK;
}

bool bt_receiver_is_streaming(void)
{
    bt_receiver_status_t status = {0};

    if (bt_receiver_get_status(
            &status
        ) != ESP_OK) {

        return false;
    }

    return status.streaming;
}

bool bt_receiver_is_connected(void)
{
    bt_receiver_status_t status = {0};

    if (bt_receiver_get_status(
            &status
        ) != ESP_OK) {

        return false;
    }

    return status.connected;
}