#include "audio.h"

#include <stdint.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

#define AUDIO_BLOCK_FRAMES       256
#define AUDIO_BLOCK_SAMPLES      (AUDIO_BLOCK_FRAMES * AUDIO_CHANNEL_COUNT)
#define AUDIO_BLOCK_BYTES        (AUDIO_BLOCK_SAMPLES * sizeof(int16_t))

/*
 * Rund 186 ms Puffer:
 * 8 Blöcke × 256 Frames ÷ 44100 Hz.
 */
#define AUDIO_RING_BUFFER_BYTES  (AUDIO_BLOCK_BYTES * 8)

#define AUDIO_TASK_STACK_SIZE    4096
#define AUDIO_TASK_PRIORITY      6

#define AUDIO_FADE_TIME_MS       250

#define I2S_BCK_GPIO             GPIO_NUM_4
#define I2S_WS_GPIO              GPIO_NUM_5
#define I2S_DATA_GPIO            GPIO_NUM_6

static const char *TAG = "audio";

static i2s_chan_handle_t tx_channel = NULL;
static RingbufHandle_t audio_ring_buffer = NULL;
static TaskHandle_t audio_task_handle = NULL;

static float target_volume = 0.10f;
static float active_gain = 0.0f;
static bool current_mute = false;

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
        ((float)AUDIO_SAMPLE_RATE_HZ * AUDIO_FADE_TIME_MS) / 1000.0f;

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
            pdMS_TO_TICKS(20),
            AUDIO_BLOCK_BYTES
        );

        if (received == NULL) {
            /*
             * Ohne neue Daten läuft I2S mit digitaler Stille weiter.
             * So bleibt kein alter DMA-Inhalt hörbar hängen.
             */
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

    ESP_LOGI(TAG, "Initialisiere I2S fuer PCM5102A");

    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(
            I2S_NUM_AUTO,
            I2S_ROLE_MASTER
        );

    ESP_RETURN_ON_ERROR(
        i2s_new_channel(
            &channel_config,
            &tx_channel,
            NULL
        ),
        TAG,
        "I2S-Kanal konnte nicht erstellt werden"
    );

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

    ESP_RETURN_ON_ERROR(
        i2s_channel_init_std_mode(
            tx_channel,
            &standard_config
        ),
        TAG,
        "I2S konnte nicht konfiguriert werden"
    );

    ESP_RETURN_ON_ERROR(
        i2s_channel_enable(tx_channel),
        TAG,
        "I2S konnte nicht aktiviert werden"
    );

    ESP_LOGI(
        TAG,
        "I2S aktiv: BCK=%d, LCK=%d, DATA=%d, %d Hz",
        I2S_BCK_GPIO,
        I2S_WS_GPIO,
        I2S_DATA_GPIO,
        AUDIO_SAMPLE_RATE_HZ
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

    BaseType_t result = xTaskCreate(
        audio_output_task,
        "audio_output",
        AUDIO_TASK_STACK_SIZE,
        NULL,
        AUDIO_TASK_PRIORITY,
        &audio_task_handle
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

esp_err_t audio_write_silence(uint32_t duration_ms)
{
    int16_t silence[AUDIO_BLOCK_SAMPLES] = {0};

    uint32_t frames_remaining =
        ((uint64_t)AUDIO_SAMPLE_RATE_HZ * duration_ms) /
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