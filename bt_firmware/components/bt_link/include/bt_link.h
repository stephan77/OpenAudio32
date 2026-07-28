#ifndef BT_LINK_H
#define BT_LINK_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bt_link_init(void);

esp_err_t bt_link_send(
    const char *message
);

#ifdef __cplusplus
}
#endif

#endif