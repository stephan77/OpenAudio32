#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mp3_decoder_init(void);

esp_err_t mp3_decoder_feed(
    const uint8_t *data,
    size_t length
);

esp_err_t mp3_decoder_finish(void);

void mp3_decoder_deinit(void);

#ifdef __cplusplus
}
#endif