#include "bt_link.h"

#include <stddef.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "bt_link";

#define BT_LINK_UART_PORT UART_NUM_2

/*
 * WROOM:
 * TX GPIO17 -> RX des ESP32-S3
 * RX GPIO16 <- TX des ESP32-S3
 */
#define BT_LINK_TX_GPIO 17
#define BT_LINK_RX_GPIO 16

#define BT_LINK_BAUD_RATE 115200
#define BT_LINK_RX_BUFFER_SIZE 1024

static bool s_initialized = false;

esp_err_t bt_link_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    const uart_config_t uart_config = {
        .baud_rate = BT_LINK_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t result =
        uart_driver_install(
            BT_LINK_UART_PORT,
            BT_LINK_RX_BUFFER_SIZE,
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

        return result;
    }

    result =
        uart_param_config(
            BT_LINK_UART_PORT,
            &uart_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "UART-Konfiguration fehlgeschlagen: %s",
            esp_err_to_name(result)
        );

        uart_driver_delete(
            BT_LINK_UART_PORT
        );

        return result;
    }

    result =
        uart_set_pin(
            BT_LINK_UART_PORT,
            BT_LINK_TX_GPIO,
            BT_LINK_RX_GPIO,
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
            BT_LINK_UART_PORT
        );

        return result;
    }

    s_initialized = true;

    ESP_LOGI(
        TAG,
        "UART-Link bereit: UART%d, TX=%d, RX=%d, %d Baud",
        BT_LINK_UART_PORT,
        BT_LINK_TX_GPIO,
        BT_LINK_RX_GPIO,
        BT_LINK_BAUD_RATE
    );

    return ESP_OK;
}

esp_err_t bt_link_send(
    const char *message
)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (message == NULL ||
        message[0] == '\0') {

        return ESP_ERR_INVALID_ARG;
    }

    const int message_length =
        strlen(message);

    const int written_message =
        uart_write_bytes(
            BT_LINK_UART_PORT,
            message,
            message_length
        );

    if (written_message != message_length) {
        return ESP_FAIL;
    }

    const int written_newline =
        uart_write_bytes(
            BT_LINK_UART_PORT,
            "\n",
            1
        );

    if (written_newline != 1) {
        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "UART gesendet: %s",
        message
    );

    return ESP_OK;
}