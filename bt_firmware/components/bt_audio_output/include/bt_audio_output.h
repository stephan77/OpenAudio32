#ifndef BT_AUDIO_OUTPUT_H
#define BT_AUDIO_OUTPUT_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bt_audio_output_init(void);

esp_err_t bt_audio_output_set_sample_rate(
    uint32_t sample_rate_hz
);

esp_err_t bt_audio_output_write(
    const void *data,
    size_t data_size
);

#ifdef __cplusplus
}
#endif

#endif