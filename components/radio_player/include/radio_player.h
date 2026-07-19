#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t radio_player_init(void);

esp_err_t radio_player_play_station(uint32_t station_id);

esp_err_t radio_player_play_current(void);

esp_err_t radio_player_stop(uint32_t timeout_ms);


bool radio_player_is_running(void);

uint32_t radio_player_get_station_id(void);

#ifdef __cplusplus
}
#endif