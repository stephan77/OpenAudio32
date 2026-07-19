#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus

extern "C" {

#endif

typedef enum {

    SPOTIFY_PLAYER_STATE_DISABLED = 0,

    SPOTIFY_PLAYER_STATE_STARTING,

    SPOTIFY_PLAYER_STATE_WAITING_FOR_LOGIN,

    SPOTIFY_PLAYER_STATE_CONNECTING,

    SPOTIFY_PLAYER_STATE_READY,

    SPOTIFY_PLAYER_STATE_PLAYING,

    SPOTIFY_PLAYER_STATE_PAUSED,

    SPOTIFY_PLAYER_STATE_ERROR

} spotify_player_state_t;

esp_err_t spotify_player_init(void);

esp_err_t spotify_player_start(void);

esp_err_t spotify_player_stop(void);

bool spotify_player_is_running(void);

spotify_player_state_t spotify_player_get_state(void);

const char *spotify_player_get_state_name(void);
esp_err_t spotify_player_zeroconf_get_handler(
    httpd_req_t *request
);

esp_err_t spotify_player_zeroconf_post_handler(
    httpd_req_t *request
);

#ifdef __cplusplus

}

#endif