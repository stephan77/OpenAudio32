#ifndef OPENAUDIO32_AIRPLAY_PCM_TASK_H
#define OPENAUDIO32_AIRPLAY_PCM_TASK_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t airplay_pcm_task_start(void);

esp_err_t airplay_pcm_task_stop(void);

bool airplay_pcm_task_is_running(void);

#ifdef __cplusplus
}
#endif

#endif