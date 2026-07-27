#include "audio.h"

#include <stdint.h>
#include <string.h>
#include "audio_dsp.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define AUDIO_BLOCK_FRAMES       256
#define AUDIO_BLOCK_SAMPLES      (AUDIO_BLOCK_FRAMES * AUDIO_CHANNEL_COUNT)
#define AUDIO_BLOCK_BYTES        (AUDIO_BLOCK_SAMPLES * sizeof(int16_t))

/*
 * Rund 186 ms Puffer:
 * 8 Blöcke × 256 Frames ÷ 44100 Hz.
 */
#define AUDIO_RING_BUFFER_BYTES  (128 * 1024)

#define AUDIO_TASK_STACK_SIZE    4096
#define AUDIO_TASK_PRIORITY      6

#define AUDIO_FADE_TIME_MS       250

#define I2S_BCK_GPIO             GPIO_NUM_4
#define I2S_WS_GPIO              GPIO_NUM_5
#define I2S_DATA_GPIO            GPIO_NUM_6


static bool playback_active = false;
static uint32_t underrun_count = 0;
static const char *TAG = "audio";

static i2s_chan_handle_t tx_channel = NULL;
static RingbufHandle_t audio_ring_buffer = NULL;
static TaskHandle_t audio_task_handle = NULL;
static SemaphoreHandle_t i2s_mutex = NULL;

static float target_volume = 0.10f;
static float active_gain = 0.0f;
static bool current_mute = false;
static uint32_t current_sample_rate =
    AUDIO_SAMPLE_RATE_HZ;
static float clamp_volume(float volume)
{
    if (volume < 0.0f) {
        return 0.0f;
    }

    if (volume > 1.0f) {
        return 1.0f;
    }

    return volume;
}

static float calculate_gain_step(void)
{
    const float frames_per_fade =
        ((float)current_sample_rate * AUDIO_FADE_TIME_MS)/ 1000.0f;

    return frames_per_fade > 1.0f
        ? 1.0f / frames_per_fade
        : 1.0f;
}

static esp_err_t write_processed_block(
    const int16_t *input,
    size_t frame_count
)
{
    int16_t output[AUDIO_BLOCK_SAMPLES];

    const float requested_gain =
        current_mute ? 0.0f : target_volume;

    const float gain_step = calculate_gain_step();

    for (size_t frame = 0; frame < frame_count; frame++) {
        if (active_gain < requested_gain) {
            active_gain += gain_step;

            if (active_gain > requested_gain) {
                active_gain = requested_gain;
            }
        } else if (active_gain > requested_gain) {
            active_gain -= gain_step;

            if (active_gain < requested_gain) {
                active_gain = requested_gain;
            }
        }

        const size_t index = frame * AUDIO_CHANNEL_COUNT;

        output[index] =
            (int16_t)((float)input[index] * active_gain);

        output[index + 1] =
            (int16_t)((float)input[index + 1] * active_gain);
    }
    esp_err_t dsp_result =

        audio_dsp_process_s16(

            output,

            frame_count

        );

    if (dsp_result != ESP_OK) {

        ESP_LOGW(

            TAG,

            "DSP-Verarbeitung fehlgeschlagen: %s",

            esp_err_to_name(dsp_result)

        );

    }
    const size_t bytes_to_write =
        frame_count * AUDIO_CHANNEL_COUNT * sizeof(int16_t);

    size_t bytes_written = 0;

    ESP_RETURN_ON_ERROR(
        i2s_channel_write(
            tx_channel,
            output,
            bytes_to_write,
            &bytes_written,
            portMAX_DELAY
        ),
        TAG,
        "I2S-Schreibfehler"
    );

    return bytes_written == bytes_to_write
        ? ESP_OK
        : ESP_FAIL;
}

static void audio_output_task(void *argument)
{
    ESP_LOGI(TAG, "Audio-Ausgabe-Task gestartet");

    int16_t silence[AUDIO_BLOCK_SAMPLES] = {0};

    while (true) {
        size_t received_size = 0;

uint8_t *received = xRingbufferReceiveUpTo(
    audio_ring_buffer,
    &received_size,
    pdMS_TO_TICKS(10),
    AUDIO_BLOCK_BYTES
);

if (received == NULL) {
    if (playback_active) {
        underrun_count++;

        if (underrun_count <= 10 || (underrun_count % 100) == 0) {
            ESP_LOGW(
                TAG,
                "Audiopuffer-Unterlauf, Anzahl: %u",
                (unsigned int)underrun_count
            );
        }
    }

    write_processed_block(silence, AUDIO_BLOCK_FRAMES);
    continue;
}

        const size_t bytes_per_frame =
            AUDIO_CHANNEL_COUNT * sizeof(int16_t);

        const size_t frame_count =
            received_size / bytes_per_frame;

        if (frame_count > 0) {
            write_processed_block(
                (const int16_t *)received,
                frame_count
            );
        }

        vRingbufferReturnItem(audio_ring_buffer, received);
    }
}


esp_err_t audio_init(void)
{
    if (tx_channel != NULL) {
        return ESP_OK;
    }

    ESP_LOGI(
        TAG,
        "Initialisiere I2S fuer PCM5102A"
    );

    i2s_mutex =
        xSemaphoreCreateMutex();

    if (i2s_mutex == NULL) {
        ESP_LOGE(
            TAG,
            "I2S-Mutex konnte nicht erstellt werden"
        );

        return ESP_ERR_NO_MEM;
    }

    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(
            I2S_NUM_AUTO,
            I2S_ROLE_MASTER
        );

    esp_err_t result =
        i2s_new_channel(
            &channel_config,
            &tx_channel,
            NULL
        );

    if (result != ESP_OK) {
        vSemaphoreDelete(i2s_mutex);
        i2s_mutex = NULL;

        ESP_LOGE(
            TAG,
            "I2S-Kanal konnte nicht erstellt werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    i2s_std_config_t standard_config = {
        .clk_cfg =
            I2S_STD_CLK_DEFAULT_CONFIG(
                AUDIO_SAMPLE_RATE_HZ
            ),

        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_16BIT,
                I2S_SLOT_MODE_STEREO
            ),

        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_GPIO,
            .ws = I2S_WS_GPIO,
            .dout = I2S_DATA_GPIO,
            .din = I2S_GPIO_UNUSED,

            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    result =
        i2s_channel_init_std_mode(
            tx_channel,
            &standard_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2S konnte nicht konfiguriert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    result =
        i2s_channel_enable(
            tx_channel
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2S konnte nicht aktiviert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }
result =
    audio_dsp_init(
        AUDIO_SAMPLE_RATE_HZ,
        AUDIO_CHANNEL_COUNT
    );

if (result != ESP_OK) {
    ESP_LOGE(
        TAG,
        "Audio-DSP konnte nicht initialisiert werden: %s",
        esp_err_to_name(result)
    );

    return result;
}
    current_sample_rate =
        AUDIO_SAMPLE_RATE_HZ;

    ESP_LOGI(
        TAG,
        "I2S aktiv: BCK=%d, LCK=%d, DATA=%d, %u Hz",
        I2S_BCK_GPIO,
        I2S_WS_GPIO,
        I2S_DATA_GPIO,
        (unsigned int)current_sample_rate
    );

    return ESP_OK;
}

esp_err_t audio_start(void)
{
    if (tx_channel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (audio_task_handle != NULL) {
        return ESP_OK;
    }

    audio_ring_buffer = xRingbufferCreate(
        AUDIO_RING_BUFFER_BYTES,
        RINGBUF_TYPE_BYTEBUF
    );

    if (audio_ring_buffer == NULL) {
        ESP_LOGE(TAG, "Audiopuffer konnte nicht erstellt werden");
        return ESP_ERR_NO_MEM;
    }

BaseType_t result = xTaskCreatePinnedToCore(

    audio_output_task,

    "audio_output",

    AUDIO_TASK_STACK_SIZE,

    NULL,

    AUDIO_TASK_PRIORITY,

    &audio_task_handle,

    0

);

    if (result != pdPASS) {
        vRingbufferDelete(audio_ring_buffer);
        audio_ring_buffer = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "Audiopuffer gestartet: %u Bytes",
        (unsigned int)AUDIO_RING_BUFFER_BYTES
    );

    return ESP_OK;
}

esp_err_t audio_submit(
    const int16_t *samples,
    size_t frame_count,
    uint32_t timeout_ms
)
{
    if (audio_ring_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (samples == NULL || frame_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t total_bytes =
        frame_count * AUDIO_CHANNEL_COUNT * sizeof(int16_t);

    const uint8_t *cursor = (const uint8_t *)samples;
    size_t bytes_remaining = total_bytes;

    while (bytes_remaining > 0) {
        const size_t block_bytes =
            bytes_remaining > AUDIO_BLOCK_BYTES
                ? AUDIO_BLOCK_BYTES
                : bytes_remaining;

        BaseType_t result = xRingbufferSend(
            audio_ring_buffer,
            cursor,
            block_bytes,
            pdMS_TO_TICKS(timeout_ms)
        );

        if (result != pdTRUE) {
            ESP_LOGW(TAG, "Audiopuffer ist voll");
            return ESP_ERR_TIMEOUT;
        }

        cursor += block_bytes;
        bytes_remaining -= block_bytes;
    }

    return ESP_OK;
}

esp_err_t audio_flush(uint32_t timeout_ms)
{
    if (audio_ring_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks =
        pdMS_TO_TICKS(timeout_ms);

    while (xRingbufferGetCurFreeSize(audio_ring_buffer) <
           AUDIO_RING_BUFFER_BYTES) {

        if ((xTaskGetTickCount() - start) >= timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return ESP_OK;
}
esp_err_t audio_clear_buffer(void)
{
    if (audio_ring_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (audio_task_handle != NULL) {
        vTaskSuspend(audio_task_handle);
    }

    size_t item_size = 0U;

    while (true) {
        void *item = xRingbufferReceive(
            audio_ring_buffer,
            &item_size,
            0
        );

        if (item == NULL) {
            break;
        }

        vRingbufferReturnItem(
            audio_ring_buffer,
            item
        );
    }

    active_gain = 0.0f;

    if (audio_task_handle != NULL) {
        vTaskResume(audio_task_handle);
    }

    ESP_LOGI(TAG, "Audiopuffer sofort geleert");

    return ESP_OK;
}

size_t audio_get_buffered_bytes(void)
{
    if (audio_ring_buffer == NULL) {
        return 0U;
    }

    const size_t free_bytes =
        xRingbufferGetCurFreeSize(audio_ring_buffer);

    return free_bytes < AUDIO_RING_BUFFER_BYTES
        ? AUDIO_RING_BUFFER_BYTES - free_bytes
        : 0U;
}
void audio_set_playback_active(bool active)
{
    playback_active = active;

    if (active) {
        underrun_count = 0;
    }

    ESP_LOGI(
        TAG,
        "Wiedergabe: %s",
        active ? "AKTIV" : "INAKTIV"
    );
}

void audio_set_volume(float volume)
{
    target_volume = clamp_volume(volume);

    ESP_LOGI(
        TAG,
        "Ziellautstaerke gesetzt: %.0f %%",
        target_volume * 100.0f
    );
}

float audio_get_volume(void)
{
    return target_volume;
}

void audio_set_mute(bool mute)
{
    current_mute = mute;

    ESP_LOGI(
        TAG,
        "Mute: %s",
        mute ? "EIN" : "AUS"
    );
}

bool audio_is_muted(void)
{
    return current_mute;
}
uint32_t audio_get_sample_rate(void)
{
    return current_sample_rate;
}

esp_err_t audio_set_sample_rate(
    uint32_t sample_rate
)
{
    if (tx_channel == NULL ||
        i2s_mutex == NULL) {

        return ESP_ERR_INVALID_STATE;
    }

    if (sample_rate != 44100U &&
        sample_rate != 48000U) {

        ESP_LOGE(
            TAG,
            "Nicht unterstuetzte Sample-Rate: %u Hz",
            (unsigned int)sample_rate
        );

        return ESP_ERR_NOT_SUPPORTED;
    }

    if (sample_rate == current_sample_rate) {
        return ESP_OK;
    }

    if (xSemaphoreTake(
            i2s_mutex,
            pdMS_TO_TICKS(2000)
        ) != pdTRUE) {

        ESP_LOGE(
            TAG,
            "I2S-Mutex konnte nicht uebernommen werden"
        );

        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(
        TAG,
        "Stelle I2S von %u Hz auf %u Hz um",
        (unsigned int)current_sample_rate,
        (unsigned int)sample_rate
    );

    esp_err_t result =
        i2s_channel_disable(
            tx_channel
        );

    if (result != ESP_OK) {
        xSemaphoreGive(i2s_mutex);

        ESP_LOGE(
            TAG,
            "I2S konnte nicht deaktiviert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    i2s_std_clk_config_t clock_config =
        I2S_STD_CLK_DEFAULT_CONFIG(
            sample_rate
        );

    result =
        i2s_channel_reconfig_std_clock(
            tx_channel,
            &clock_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2S-Takt konnte nicht geaendert werden: %s",
            esp_err_to_name(result)
        );

        /*
         * Alten Zustand wieder starten.
         */
        const esp_err_t enable_result =
            i2s_channel_enable(
                tx_channel
            );

        if (enable_result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "I2S konnte nach Fehler nicht wieder aktiviert werden: %s",
                esp_err_to_name(enable_result)
            );
        }

        xSemaphoreGive(i2s_mutex);

        return result;
    }

    result =
        i2s_channel_enable(
            tx_channel
        );

    if (result != ESP_OK) {
        xSemaphoreGive(i2s_mutex);

        ESP_LOGE(
            TAG,
            "I2S konnte nicht wieder aktiviert werden: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    current_sample_rate =
        sample_rate;
result =
    audio_dsp_set_sample_rate(
        sample_rate
    );

if (result != ESP_OK) {
    ESP_LOGE(
        TAG,
        "DSP-Sample-Rate konnte nicht angepasst werden: %s",
        esp_err_to_name(result)
    );
}
    xSemaphoreGive(i2s_mutex);

    ESP_LOGI(
        TAG,
        "I2S-Sample-Rate jetzt %u Hz",
        (unsigned int)current_sample_rate
    );

    return ESP_OK;
}
esp_err_t audio_write_silence(uint32_t duration_ms)
{
    int16_t silence[AUDIO_BLOCK_SAMPLES] = {0};

    uint32_t frames_remaining =
        ((uint64_t)current_sample_rate * duration_ms) /
        1000U;

    while (frames_remaining > 0) {
        const size_t frames =
            frames_remaining > AUDIO_BLOCK_FRAMES
                ? AUDIO_BLOCK_FRAMES
                : frames_remaining;

        ESP_RETURN_ON_ERROR(
            audio_submit(
                silence,
                frames,
                1000
            ),
            TAG,
            "Stille konnte nicht gepuffert werden"
        );

        frames_remaining -= frames;
    }

    return ESP_OK;
}
uint32_t audio_get_underrun_count(void)
{
    return underrun_count;
}

bool audio_is_playback_active(void)
{
    return playback_active;
}