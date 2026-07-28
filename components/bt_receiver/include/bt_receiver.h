#ifndef BT_RECEIVER_H
#define BT_RECEIVER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BT_RECEIVER_STATE_OFFLINE = 0,
    BT_RECEIVER_STATE_READY,
    BT_RECEIVER_STATE_CONNECTED,
    BT_RECEIVER_STATE_STREAMING,
    BT_RECEIVER_STATE_PAUSED,
    BT_RECEIVER_STATE_ERROR
} bt_receiver_state_t;

typedef struct {
    bt_receiver_state_t state;

    bool module_ready;
    bool connected;
    bool streaming;

    uint8_t volume_percent;
    uint8_t play_status;

    char title[128];
    char artist[128];
    char album[128];
} bt_receiver_status_t;

/**
 * Initialisiert UART2 zum ESP32-WROOM und startet
 * die Empfangstask.
 */
esp_err_t bt_receiver_init(void);

/**
 * Liest eine threadsichere Kopie des aktuellen
 * Bluetooth-Status.
 */
esp_err_t bt_receiver_get_status(
    bt_receiver_status_t *status
);

/**
 * Prüft, ob aktuell Bluetooth-Audio wiedergegeben wird.
 */
bool bt_receiver_is_streaming(void);

/**
 * Prüft, ob ein Bluetooth-Gerät verbunden ist.
 */
bool bt_receiver_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif