#ifndef OPENAUDIO32_MEDIA_MANAGER_H
#define OPENAUDIO32_MEDIA_MANAGER_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MEDIA_SOURCE_NONE = 0,
    MEDIA_SOURCE_RADIO,
    MEDIA_SOURCE_SPOTIFY,
    MEDIA_SOURCE_AIRPLAY
} media_source_t;

typedef esp_err_t (*media_stop_callback_t)(void);

esp_err_t media_manager_init(void);

esp_err_t media_manager_register_source(
    media_source_t source,
    media_stop_callback_t stop_callback
);

esp_err_t media_manager_activate(
    media_source_t source
);

esp_err_t media_manager_release(
    media_source_t source
);

media_source_t media_manager_get_active_source(void);

const char *media_manager_source_name(
    media_source_t source
);

#ifdef __cplusplus
}
#endif

#endif