#include "wav_player.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "audio.h"
#include "esp_log.h"

static const char *TAG = "wav_player";

/*
 * Diese Symbole erzeugt ESP-IDF aus:
 * assets/test.wav
 *
 * Sonderzeichen wie / und . werden zu Unterstrichen.
 */
extern const uint8_t test_wav_start[]

    asm("_binary_test_wav_start");

extern const uint8_t test_wav_end[]

    asm("_binary_test_wav_end");

typedef struct __attribute__((packed))
{
    char riff_id[4];
    uint32_t riff_size;
    char wave_id[4];
} wav_riff_header_t;

typedef struct __attribute__((packed))
{
    uint16_t audio_format;
    uint16_t channel_count;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_format_t;

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0]
         | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0]
         | ((uint32_t)data[1] << 8)
         | ((uint32_t)data[2] << 16)
         | ((uint32_t)data[3] << 24);
}

esp_err_t wav_play_embedded_test(void)
{
    const uint8_t *file_start = test_wav_start;
    const uint8_t *file_end = test_wav_end;
    const size_t file_size = (size_t)(file_end - file_start);

    ESP_LOGI(TAG, "WAV-Dateigroesse: %u Bytes", (unsigned int)file_size);

    if (file_size < sizeof(wav_riff_header_t)) {
        ESP_LOGE(TAG, "WAV-Datei ist zu klein");
        return ESP_ERR_INVALID_SIZE;
    }

    if (memcmp(file_start, "RIFF", 4) != 0 ||
        memcmp(file_start + 8, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Keine gueltige RIFF/WAVE-Datei");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const uint8_t *cursor = file_start + 12;

    wav_format_t format = {0};

    const uint8_t *audio_data = NULL;
    size_t audio_data_size = 0;

    while ((cursor + 8) <= file_end) {
        const uint8_t *chunk_id = cursor;
        uint32_t chunk_size = read_u32_le(cursor + 4);

        cursor += 8;

        if ((cursor + chunk_size) > file_end) {
            ESP_LOGE(TAG, "Ungueltige Chunk-Groesse");
            return ESP_ERR_INVALID_SIZE;
        }

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                ESP_LOGE(TAG, "fmt-Chunk ist zu klein");
                return ESP_ERR_INVALID_SIZE;
            }

            format.audio_format = read_u16_le(cursor + 0);
            format.channel_count = read_u16_le(cursor + 2);
            format.sample_rate = read_u32_le(cursor + 4);
            format.byte_rate = read_u32_le(cursor + 8);
            format.block_align = read_u16_le(cursor + 12);
            format.bits_per_sample = read_u16_le(cursor + 14);

            ESP_LOGI(
                TAG,
                "WAV: Format=%u, Kanaele=%u, Rate=%u Hz, Bit=%u",
                format.audio_format,
                format.channel_count,
                (unsigned int)format.sample_rate,
                format.bits_per_sample
            );
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            audio_data = cursor;
            audio_data_size = chunk_size;
        }

        /*
         * RIFF-Chunks werden auf gerade Byteanzahl aufgefüllt.
         */
        cursor += chunk_size + (chunk_size & 1U);
    }

    if (audio_data == NULL || audio_data_size == 0) {
        ESP_LOGE(TAG, "Kein data-Chunk gefunden");
        return ESP_ERR_NOT_FOUND;
    }

    if (format.audio_format != 1) {
        ESP_LOGE(TAG, "Nur PCM-WAV wird unterstuetzt");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (format.channel_count != AUDIO_CHANNEL_COUNT) {
        ESP_LOGE(
            TAG,
            "WAV muss Stereo sein, gefunden: %u Kanaele",
            format.channel_count
        );
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (format.sample_rate != AUDIO_SAMPLE_RATE_HZ) {
        ESP_LOGE(
            TAG,
            "WAV muss %d Hz haben, gefunden: %u Hz",
            AUDIO_SAMPLE_RATE_HZ,
            (unsigned int)format.sample_rate
        );
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (format.bits_per_sample != 16) {
        ESP_LOGE(
            TAG,
            "WAV muss 16 Bit haben, gefunden: %u Bit",
            format.bits_per_sample
        );
        return ESP_ERR_NOT_SUPPORTED;
    }

    if ((audio_data_size % 4) != 0) {
        ESP_LOGW(TAG, "PCM-Datengroesse ist nicht durch 4 teilbar");
    }

    size_t frame_count = audio_data_size /
                         (AUDIO_CHANNEL_COUNT * sizeof(int16_t));

    ESP_LOGI(
        TAG,
        "Starte WAV-Wiedergabe: %u Frames",
        (unsigned int)frame_count
    );

esp_err_t result = audio_submit(
    (const int16_t *)audio_data,
    frame_count,
    2000
);
if (result == ESP_OK) {
    result = audio_flush(5000);
}

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "WAV-Wiedergabe fehlgeschlagen: %s",
            esp_err_to_name(result)
        );
        return result;
    }



esp_err_t silence_result = audio_write_silence(300);

if (silence_result != ESP_OK) {
    ESP_LOGE(
        TAG,
        "Stille konnte nicht ausgegeben werden: %s",
        esp_err_to_name(silence_result)
    );
    return silence_result;
}

ESP_LOGI(TAG, "WAV-Wiedergabe sauber beendet");

return ESP_OK;
}