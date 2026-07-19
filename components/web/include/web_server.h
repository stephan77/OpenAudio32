#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Startet das OpenAudio32-Webinterface.
 */
esp_err_t web_server_start(void);

/**
 * Stoppt das Webinterface.
 */
esp_err_t web_server_stop(void);

#ifdef __cplusplus
}
#endif