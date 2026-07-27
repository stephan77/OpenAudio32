#include "airplay_pcm_task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "audio.h"
#include "audio_receiver.h"
#include "media_manager.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AIRPLAY_FRAME_COUNT       352U
#define AIRPLAY_CHANNEL_COUNT     2U

#define AIRPLAY_PCM_TASK_STACK    6144U
#define AIRPLAY_PCM_TASK_PRIORITY 8U
#define AIRPLAY_PCM_TASK_CORE     1

/*
 * Nicht nur einen einzigen AirPlay-Frame puffern.
 *
 * 352 Samples × 2 Kanäle × 2 Byte = 1408 Byte pro Frame.
 * Vier Frames entsprechen etwa 32 ms Audio bei 44,1 kHz.
 */
#define AIRPLAY_OUTPUT_LIMIT_BYTES (32U * 1024U)
#define AIRPLAY_PREBUFFER_BYTES    (16U * 1024U)



static const char *TAG = "airplay_pcm";

static TaskHandle_t task_handle = NULL;
static volatile bool task_running = false;

/*
 * Mindestens einen vollständigen FreeRTOS-Tick blockieren.
 *
 * pdMS_TO_TICKS(1) oder pdMS_TO_TICKS(5) können bei 100 Hz
 * Tickrate zu 0 werden.
 */
static inline void delay_one_tick(void)
{
    vTaskDelay(1);
}

static bool wait_for_output_space(void)
{
    while (task_running) {
        if (media_manager_get_active_source() != MEDIA_SOURCE_AIRPLAY) {
            return false;
        }

        if (audio_get_buffered_bytes() < AIRPLAY_OUTPUT_LIMIT_BYTES) {
            return true;
        }

        vTaskDelay(1);
    }

    return false;
}

static void pcm_task(void *argument)

{

    (void)argument;

    int16_t *pcm = malloc(

        (AIRPLAY_FRAME_COUNT + 1U) *

        AIRPLAY_CHANNEL_COUNT *

        sizeof(int16_t)

    );

    if (pcm == NULL) {

        ESP_LOGE(TAG, "PCM-Arbeitspuffer konnte nicht reserviert werden");

        task_running = false;

        task_handle = NULL;

        vTaskDelete(NULL);

        return;

    }

    ESP_LOGI(TAG, "AirPlay-PCM-Task gestartet");

    bool playback_announced = false;

    unsigned frames_since_yield = 0;

    while (task_running) {

        if (media_manager_get_active_source() != MEDIA_SOURCE_AIRPLAY) {

            if (playback_announced) {

                audio_set_playback_active(false);

                playback_announced = false;

            }

            vTaskDelay(pdMS_TO_TICKS(20));

            continue;

        }

        if (!wait_for_output_space()) {

            vTaskDelay(1);

            continue;

        }

        if (!audio_receiver_has_data()) {

            vTaskDelay(1);

            continue;

        }

        const size_t frame_count = audio_receiver_read(

            pcm,

            AIRPLAY_FRAME_COUNT + 1U

        );

        if (frame_count == 0U) {

            vTaskDelay(1);

            continue;

        }

        const esp_err_t result = audio_submit(

            pcm,

            frame_count,

            100U

        );

        if (result == ESP_ERR_TIMEOUT) {

            vTaskDelay(1);

            continue;

        }

        if (result != ESP_OK) {

            ESP_LOGE(

                TAG,

                "PCM-Übergabe fehlgeschlagen: %s",

                esp_err_to_name(result)

            );

            vTaskDelay(1);

            continue;

        }

        if (!playback_announced &&

            audio_get_buffered_bytes() >= AIRPLAY_PREBUFFER_BYTES) {

            audio_set_playback_active(true);

            playback_announced = true;

            ESP_LOGI(

                TAG,

                "AirPlay-Wiedergabe gestartet, Vorpuffer=%u Byte",

                (unsigned)audio_get_buffered_bytes()

            );

        }

        /*

         * Nicht nach jedem 8-ms-Audioframe 10 ms schlafen.

         * Nur gelegentlich CPU 1 für den Idle-Task freigeben.

         */

        frames_since_yield++;

        if (frames_since_yield >= 8U) {

            vTaskDelay(1);

            frames_since_yield = 0;

        }

    }

    audio_set_playback_active(false);

    free(pcm);

    task_running = false;

    task_handle = NULL;

    ESP_LOGI(TAG, "AirPlay-PCM-Task beendet");

    vTaskDelete(NULL);

}

esp_err_t airplay_pcm_task_start(void)
{
    if (task_handle != NULL) {
        return ESP_OK;
    }

    task_running = true;

    const BaseType_t result =
        xTaskCreatePinnedToCore(
            pcm_task,
            "airplay_pcm",
            AIRPLAY_PCM_TASK_STACK,
            NULL,
            AIRPLAY_PCM_TASK_PRIORITY,
            &task_handle,
            AIRPLAY_PCM_TASK_CORE
        );

    if (result != pdPASS) {
        task_running = false;
        task_handle = NULL;

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t airplay_pcm_task_stop(void)
{
    if (task_handle == NULL) {
        return ESP_OK;
    }

    task_running = false;

    int remaining = 100;

    while (task_handle != NULL &&
           remaining-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (task_handle != NULL) {
        ESP_LOGE(
            TAG,
            "PCM-Task wurde nicht rechtzeitig beendet"
        );

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

bool airplay_pcm_task_is_running(void)
{
    return task_handle != NULL &&
           task_running;
}