#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "mp3_decoder.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "audio.h"
#include "esp_check.h"
#include "esp_log.h"

#define MP3_INPUT_BUFFER_SIZE (64 * 1024)
//#define MP3_MIN_DECODE_BYTES  16384
#define MP3_MIN_DECODE_BYTES 2048U
#define MP3_MAX_SAMPLES_PER_FRAME (MINIMP3_MAX_SAMPLES_PER_FRAME)


static const char *TAG = "mp3_decoder";

static mp3dec_t decoder;

static uint8_t input_buffer[MP3_INPUT_BUFFER_SIZE];
static size_t input_length = 0;

static mp3d_sample_t pcm_buffer[MP3_MAX_SAMPLES_PER_FRAME];

static bool initialized = false;
static uint32_t decoded_frames = 0;

esp_err_t mp3_decoder_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    mp3dec_init(&decoder);

    input_length = 0;
    decoded_frames = 0;
    initialized = true;

    ESP_LOGI(TAG, "minimp3 initialisiert");

    return ESP_OK;
}

static esp_err_t decode_available_frames(bool end_of_stream)
{
    while (input_length > 0) {
        if (!end_of_stream &&
            input_length < MP3_MIN_DECODE_BYTES) {
            break;
        }

        mp3dec_frame_info_t frame_info = {0};

        int samples_per_channel = mp3dec_decode_frame(
            &decoder,
            input_buffer,
            (int)input_length,
            pcm_buffer,
            &frame_info
        );

        if (frame_info.frame_bytes <= 0) {
            break;
        }

        if ((size_t)frame_info.frame_bytes > input_length) {
            ESP_LOGE(TAG, "Ungueltige MP3-Framegroesse");
            return ESP_FAIL;
        }

        if (samples_per_channel > 0) {
    if (frame_info.channels != AUDIO_CHANNEL_COUNT) {
        ESP_LOGE(
            TAG,
            "Nur Stereo unterstuetzt, gefunden: %d",
            frame_info.channels
        );

        return ESP_ERR_NOT_SUPPORTED;
    }

    if (frame_info.hz != 44100 &&
        frame_info.hz != 48000) {

        ESP_LOGE(
            TAG,
            "Sample-Rate nicht unterstuetzt: %d Hz",
            frame_info.hz
        );

        return ESP_ERR_NOT_SUPPORTED;
    }

    if ((uint32_t)frame_info.hz !=
        audio_get_sample_rate()) {

        ESP_RETURN_ON_ERROR(
            audio_set_sample_rate(
                (uint32_t)frame_info.hz
            ),
            TAG,
            "I2S-Sample-Rate konnte nicht angepasst werden"
        );
    }

    const bool first_frame =
        decoded_frames == 0;

    ESP_RETURN_ON_ERROR(
        audio_submit(
            (const int16_t *)pcm_buffer,
            (size_t)samples_per_channel,
            5000
        ),
        TAG,
        "PCM-Ausgabe fehlgeschlagen"
    );

    decoded_frames++;

    if (first_frame) {
        audio_set_playback_active(true);

        ESP_LOGI(
            TAG,
            "Erster MP3-Frame: %d Hz, %d Kanaele, %d kbps",
            frame_info.hz,
            frame_info.channels,
            frame_info.bitrate_kbps
        );
    }
}

        const size_t consumed =
            (size_t)frame_info.frame_bytes;

        const size_t remaining =
            input_length - consumed;

        memmove(
            input_buffer,
            input_buffer + consumed,
            remaining
        );

        input_length = remaining;
    }

    return ESP_OK;
}

esp_err_t mp3_decoder_feed(
    const uint8_t *data,
    size_t length
)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    while (length > 0) {
        size_t free_space =
            MP3_INPUT_BUFFER_SIZE - input_length;

        if (free_space == 0) {
            esp_err_t result = decode_available_frames(false);

            if (result != ESP_OK) {
                return result;
            }

            free_space =
                MP3_INPUT_BUFFER_SIZE - input_length;

            if (free_space == 0) {
                ESP_LOGE(
                    TAG,
                    "MP3-Eingangspuffer voll, aber kein Frame decodierbar"
                );

                return ESP_ERR_INVALID_RESPONSE;
            }
        }

        const size_t copy_length =
            length < free_space
                ? length
                : free_space;

        memcpy(
            input_buffer + input_length,
            data,
            copy_length
        );

        input_length += copy_length;
        data += copy_length;
        length -= copy_length;

        esp_err_t result = decode_available_frames(false);

        if (result != ESP_OK) {
            return result;
        }
    }

    return ESP_OK;
}

esp_err_t mp3_decoder_finish(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = decode_available_frames(true);

    if (result != ESP_OK) {
        return result;
    }

    if (input_length > 0) {
        ESP_LOGW(
            TAG,
            "%u nicht decodierte MP3-Restbytes verworfen",
            (unsigned int)input_length
        );
    }

    ESP_LOGI(
        TAG,
        "MP3-Decodierung beendet, Frames: %u",
        (unsigned int)decoded_frames
    );

    input_length = 0;

    return audio_flush(15000);
}

void mp3_decoder_deinit(void)
{
    input_length = 0;
    decoded_frames = 0;
    initialized = false;

    ESP_LOGI(TAG, "MP3-Decoder beendet");
}