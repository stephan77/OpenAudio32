#include "audio_output.h"

#include <stddef.h>
#include <stdint.h>

#include "audio.h"

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"

#define AIRPLAY_BYTES_PER_STEREO_FRAME \
    (2U * sizeof(int16_t))

#define OPENAUDIO32_I2S_BLOCK_FRAMES 256U

static const char *TAG =
    "airplay_adapter";

static audio_channel_mode_t channel_mode =
    AUDIO_CHANNEL_STEREO;

esp_err_t audio_output_init(void)
{
    ESP_LOGI(
        TAG,
        "AirPlay verwendet die OpenAudio32-Audioengine"
    );

    return ESP_OK;
}

void audio_output_start(void)
{
    /*
     * Kein eigener I2S-Task.
     * Die OpenAudio32-Audioengine läuft dauerhaft.
     */
}

void audio_output_stop(void)
{
    (void)audio_clear_buffer();

    audio_set_playback_active(
        false
    );
}

void audio_output_flush(void)
{
    const esp_err_t result =
        audio_clear_buffer();

    if (result != ESP_OK &&
        result != ESP_ERR_INVALID_STATE) {

        ESP_LOGW(
            TAG,
            "Audiopuffer konnte nicht geleert werden: %s",
            esp_err_to_name(result)
        );
    }
}

esp_err_t audio_output_write(
    const void *data,
    size_t bytes,
    TickType_t wait
)
{
    if (data == NULL ||
        bytes == 0U ||
        bytes % AIRPLAY_BYTES_PER_STEREO_FRAME != 0U) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t frame_count =
        bytes / AIRPLAY_BYTES_PER_STEREO_FRAME;

    uint32_t timeout_ms;

    if (wait == portMAX_DELAY) {
        timeout_ms = 1000U;
    } else {
        timeout_ms =
            (uint32_t)pdTICKS_TO_MS(wait);
    }

    return audio_submit(
        (const int16_t *)data,
        frame_count,
        timeout_ms
    );
}

void audio_output_set_sample_rate(
    uint32_t rate
)
{
    const esp_err_t result =
        audio_set_sample_rate(
            rate
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Samplerate %u Hz konnte nicht gesetzt werden: %s",
            (unsigned int)rate,
            esp_err_to_name(result)
        );
    }
}

void audio_output_set_source_rate(
    int rate
)
{
    if (rate <= 0) {
        return;
    }

    audio_output_set_sample_rate(
        (uint32_t)rate
    );
}

uint32_t audio_output_get_hardware_latency_us(void)
{
    const uint32_t rate =
        audio_get_sample_rate();

    if (rate == 0U) {
        return 0U;
    }

    return (uint32_t)(
        ((uint64_t)OPENAUDIO32_I2S_BLOCK_FRAMES *
         1000000ULL) /
        rate
    );
}

audio_channel_mode_t audio_output_cycle_channel_mode(void)
{
    switch (channel_mode) {
    case AUDIO_CHANNEL_STEREO:
        channel_mode =
            AUDIO_CHANNEL_LEFT;
        break;

    case AUDIO_CHANNEL_LEFT:
        channel_mode =
            AUDIO_CHANNEL_RIGHT;
        break;

    case AUDIO_CHANNEL_RIGHT:
    default:
        channel_mode =
            AUDIO_CHANNEL_STEREO;
        break;
    }

    return channel_mode;
}

audio_channel_mode_t audio_output_get_channel_mode(void)
{
    return channel_mode;
}