#pragma once

#ifdef ESP_PLATFORM

#include "esp_log.h"

/*
 * cspot-Logging unter ESP-IDF direkt auf ESP_LOG umleiten.
 *
 * Der Bell-Logger verursacht insbesondere bei debug()-Aufrufen
 * einen Nullpointer-Absturz.
 */

#define CSPOT_LOG_debug(...) \
    ESP_LOGD("cspot", __VA_ARGS__)

#define CSPOT_LOG_info(...) \
    ESP_LOGI("cspot", __VA_ARGS__)

#define CSPOT_LOG_error(...) \
    ESP_LOGE("cspot", __VA_ARGS__)

#define CSPOT_LOG(type, ...) \
    CSPOT_LOG_##type(__VA_ARGS__)

#else

#include <BellLogger.h>

#define CSPOT_LOG(type, ...)                                                \
    do {                                                                    \
        bell::bellGlobalLogger->type(                                       \
            __FILE__,                                                       \
            __LINE__,                                                       \
            "cspot",                                                        \
            __VA_ARGS__                                                     \
        );                                                                  \
    } while (0)

#endif