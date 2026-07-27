#ifndef OPENAUDIO32_AIRPLAY_CORE_H
#define OPENAUDIO32_AIRPLAY_CORE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t airplay_core_init(void);

esp_err_t airplay_core_start(void);

esp_err_t airplay_core_stop(void);

#ifdef __cplusplus
}
#endif

#endif