#ifndef OPENAUDIO32_AIRPLAY_PLAYER_H
#define OPENAUDIO32_AIRPLAY_PLAYER_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AIRPLAY_PLAYER_STATE_UNINITIALIZED = 0,
    AIRPLAY_PLAYER_STATE_STOPPED,
    AIRPLAY_PLAYER_STATE_READY,
    AIRPLAY_PLAYER_STATE_CONNECTED,
    AIRPLAY_PLAYER_STATE_PLAYING,
    AIRPLAY_PLAYER_STATE_PAUSED,
    AIRPLAY_PLAYER_STATE_ERROR
} airplay_player_state_t;

esp_err_t airplay_player_init(void);

esp_err_t airplay_player_start(void);

esp_err_t airplay_player_stop(void);

bool airplay_player_is_started(void);

bool airplay_player_is_connected(void);

airplay_player_state_t airplay_player_get_state(void);

const char *airplay_player_state_name(
    airplay_player_state_t state
);

#ifdef __cplusplus
}
#endif

#endif