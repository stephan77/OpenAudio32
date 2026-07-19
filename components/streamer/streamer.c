#include "streamer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include "audio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "mp3_decoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"

#define HTTP_BUFFER_SIZE 4096
#define WAV_HEADER_BUFFER_SIZE 512

#define RADIO_RECONNECT_DELAY_MS 3000
#define RADIO_HTTP_TIMEOUT_MS    1000
#define RADIO_MAX_REDIRECTS 5
#define RADIO_URL_BUFFER_SIZE 512

typedef struct {
    char location[RADIO_URL_BUFFER_SIZE];
} radio_http_context_t;
static const char *TAG = "streamer";
static volatile bool radio_stop_requested = false;

void streamer_radio_request_stop(void)
{
    radio_stop_requested = true;
}

void streamer_radio_clear_stop(void)
{
    radio_stop_requested = false;
}

bool streamer_radio_stop_requested(void)
{
    return radio_stop_requested;
}

typedef enum {
    WAV_STATE_RIFF_HEADER,
    WAV_STATE_CHUNK_HEADER,
    WAV_STATE_FMT_CHUNK,
    WAV_STATE_SKIP_CHUNK,
    WAV_STATE_AUDIO_DATA,
    WAV_STATE_FINISHED
} wav_stream_state_t;

typedef struct {
    wav_stream_state_t state;

    uint8_t header_buffer[WAV_HEADER_BUFFER_SIZE];
    size_t header_length;

    char current_chunk_id[4];
    uint32_t current_chunk_size;
    uint32_t current_chunk_remaining;

    uint16_t audio_format;
    uint16_t channel_count;
    uint32_t sample_rate;
    uint16_t bits_per_sample;

    bool format_found;
    bool data_found;

    uint8_t pending_pcm[4];
    size_t pending_pcm_length;

    uint64_t total_pcm_bytes;
} wav_stream_parser_t;
static esp_err_t radio_http_event_handler(
    esp_http_client_event_t *event
)
{
    if (event == NULL ||
        event->user_data == NULL) {

        return ESP_OK;
    }

    radio_http_context_t *context =
        (radio_http_context_t *)event->user_data;

    if (event->event_id == HTTP_EVENT_ON_HEADER &&
        event->header_key != NULL &&
        event->header_value != NULL &&
        strcasecmp(event->header_key, "Location") == 0) {

        snprintf(
            context->location,
            sizeof(context->location),
            "%s",
            event->header_value
        );

        ESP_LOGI(
            TAG,
            "Redirect-Ziel: %s",
            context->location
        );
    }

    return ESP_OK;
}
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

static void wav_parser_init(wav_stream_parser_t *parser)
{
    memset(parser, 0, sizeof(*parser));
    parser->state = WAV_STATE_RIFF_HEADER;
}

static esp_err_t wav_validate_format(
    const wav_stream_parser_t *parser
)
{
    ESP_LOGI(
        TAG,
        "WAV: Format=%u, Kanaele=%u, Rate=%u Hz, Bit=%u",
        parser->audio_format,
        parser->channel_count,
        (unsigned int)parser->sample_rate,
        parser->bits_per_sample
    );

    if (parser->audio_format != 1) {
        ESP_LOGE(TAG, "Nur PCM-WAV wird unterstuetzt");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (parser->channel_count != AUDIO_CHANNEL_COUNT) {
        ESP_LOGE(TAG, "WAV muss Stereo sein");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (parser->sample_rate != AUDIO_SAMPLE_RATE_HZ) {
        ESP_LOGE(
            TAG,
            "WAV muss %d Hz haben",
            AUDIO_SAMPLE_RATE_HZ
        );
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (parser->bits_per_sample != 16) {
        ESP_LOGE(TAG, "WAV muss 16 Bit haben");
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

static esp_err_t wav_submit_pcm(
    wav_stream_parser_t *parser,
    const uint8_t *data,
    size_t data_length
)
{
    /*
     * Ein Stereo-Frame besteht aus:
     * 2 Kanaele × 16 Bit = 4 Byte.
     *
     * HTTP-Bloecke koennen an beliebiger Stelle enden.
     * Deshalb puffern wir maximal drei Restbytes.
     */
    if (parser->pending_pcm_length > 0) {
        const size_t needed =
            4U - parser->pending_pcm_length;

        const size_t copy_length =
            data_length < needed
                ? data_length
                : needed;

        memcpy(
            parser->pending_pcm + parser->pending_pcm_length,
            data,
            copy_length
        );

        parser->pending_pcm_length += copy_length;
        data += copy_length;
        data_length -= copy_length;

        if (parser->pending_pcm_length == 4U) {
            ESP_RETURN_ON_ERROR(
                audio_submit(
                    (const int16_t *)parser->pending_pcm,
                    1,
                    5000
                ),
                TAG,
                "PCM-Restframe konnte nicht gepuffert werden"
            );

            parser->total_pcm_bytes += 4U;
            parser->pending_pcm_length = 0;
        }
    }

    const size_t complete_bytes =
        data_length - (data_length % 4U);

    if (complete_bytes > 0) {
        const size_t frame_count =
            complete_bytes / 4U;

        ESP_RETURN_ON_ERROR(
            audio_submit(
                (const int16_t *)data,
                frame_count,
                5000
            ),
            TAG,
            "PCM-Daten konnten nicht gepuffert werden"
        );

        parser->total_pcm_bytes += complete_bytes;

        data += complete_bytes;
        data_length -= complete_bytes;
    }

    if (data_length > 0) {
        memcpy(
            parser->pending_pcm,
            data,
            data_length
        );

        parser->pending_pcm_length = data_length;
    }

    return ESP_OK;
}

static esp_err_t wav_parser_feed(
    wav_stream_parser_t *parser,
    const uint8_t *data,
    size_t data_length
)
{
    while (data_length > 0 &&
           parser->state != WAV_STATE_FINISHED) {

        switch (parser->state) {

        case WAV_STATE_RIFF_HEADER: {
            const size_t needed =
                12U - parser->header_length;

            const size_t copy_length =
                data_length < needed
                    ? data_length
                    : needed;

            memcpy(
                parser->header_buffer + parser->header_length,
                data,
                copy_length
            );

            parser->header_length += copy_length;
            data += copy_length;
            data_length -= copy_length;

            if (parser->header_length < 12U) {
                break;
            }

            if (memcmp(parser->header_buffer, "RIFF", 4) != 0 ||
                memcmp(parser->header_buffer + 8, "WAVE", 4) != 0) {

                ESP_LOGE(TAG, "Keine gueltige RIFF/WAVE-Datei");
                return ESP_ERR_INVALID_RESPONSE;
            }

            ESP_LOGI(TAG, "RIFF/WAVE-Header erkannt");

            parser->header_length = 0;
            parser->state = WAV_STATE_CHUNK_HEADER;
            break;
        }

        case WAV_STATE_CHUNK_HEADER: {
            const size_t needed =
                8U - parser->header_length;

            const size_t copy_length =
                data_length < needed
                    ? data_length
                    : needed;

            memcpy(
                parser->header_buffer + parser->header_length,
                data,
                copy_length
            );

            parser->header_length += copy_length;
            data += copy_length;
            data_length -= copy_length;

            if (parser->header_length < 8U) {
                break;
            }

            memcpy(
                parser->current_chunk_id,
                parser->header_buffer,
                4
            );

            parser->current_chunk_size =
                read_u32_le(parser->header_buffer + 4);

            parser->current_chunk_remaining =
                parser->current_chunk_size;

            parser->header_length = 0;

            ESP_LOGI(
                TAG,
                "WAV-Chunk: %.4s, %u Bytes",
                parser->current_chunk_id,
                (unsigned int)parser->current_chunk_size
            );

            if (memcmp(parser->current_chunk_id, "fmt ", 4) == 0) {
                parser->state = WAV_STATE_FMT_CHUNK;
            } else if (memcmp(parser->current_chunk_id, "data", 4) == 0) {
                ESP_RETURN_ON_ERROR(
                    wav_validate_format(parser),
                    TAG,
                    "WAV-Format wird nicht unterstuetzt"
                );

                parser->data_found = true;
                parser->state = WAV_STATE_AUDIO_DATA;

                ESP_LOGI(
                    TAG,
                    "PCM-Streaming startet: %u Bytes",
                    (unsigned int)parser->current_chunk_size
                );
            } else {
                parser->state = WAV_STATE_SKIP_CHUNK;
            }

            break;
        }

        case WAV_STATE_FMT_CHUNK: {
            const size_t wanted =
                parser->current_chunk_remaining;

            const size_t copy_length =
                data_length < wanted
                    ? data_length
                    : wanted;

            if ((parser->header_length + copy_length) >
                WAV_HEADER_BUFFER_SIZE) {

                ESP_LOGE(TAG, "fmt-Chunk ist zu gross");
                return ESP_ERR_INVALID_SIZE;
            }

            memcpy(
                parser->header_buffer + parser->header_length,
                data,
                copy_length
            );

            parser->header_length += copy_length;
            parser->current_chunk_remaining -= copy_length;
            data += copy_length;
            data_length -= copy_length;

            if (parser->current_chunk_remaining > 0) {
                break;
            }

            if (parser->header_length < 16U) {
                ESP_LOGE(TAG, "fmt-Chunk ist zu klein");
                return ESP_ERR_INVALID_SIZE;
            }

            parser->audio_format =
                read_u16_le(parser->header_buffer + 0);

            parser->channel_count =
                read_u16_le(parser->header_buffer + 2);

            parser->sample_rate =
                read_u32_le(parser->header_buffer + 4);

            parser->bits_per_sample =
                read_u16_le(parser->header_buffer + 14);

            parser->format_found = true;
            parser->header_length = 0;
            parser->state = WAV_STATE_CHUNK_HEADER;

            /*
             * RIFF-Chunks mit ungerader Laenge besitzen
             * ein Padding-Byte. Bei fmt ist das selten,
             * wird später für allgemeine Chunks berücksichtigt.
             */
            break;
        }

        case WAV_STATE_SKIP_CHUNK: {
            const size_t skip_length =
                data_length < parser->current_chunk_remaining
                    ? data_length
                    : parser->current_chunk_remaining;

            data += skip_length;
            data_length -= skip_length;
            parser->current_chunk_remaining -= skip_length;

            if (parser->current_chunk_remaining == 0) {
                parser->state = WAV_STATE_CHUNK_HEADER;
            }

            break;
        }

        case WAV_STATE_AUDIO_DATA: {
            const size_t pcm_length =
                data_length < parser->current_chunk_remaining
                    ? data_length
                    : parser->current_chunk_remaining;

            ESP_RETURN_ON_ERROR(
                wav_submit_pcm(
                    parser,
                    data,
                    pcm_length
                ),
                TAG,
                "PCM-Streaming fehlgeschlagen"
            );

            data += pcm_length;
            data_length -= pcm_length;
            parser->current_chunk_remaining -= pcm_length;

            if (parser->current_chunk_remaining == 0) {
                parser->state = WAV_STATE_FINISHED;
                ESP_LOGI(TAG, "data-Chunk vollstaendig verarbeitet");
            }

            break;
        }

        case WAV_STATE_FINISHED:
            break;
        }
    }

    return ESP_OK;
}

esp_err_t streamer_play_wav_stream(const char *url)
{
    if (url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Oeffne WAV-Stream: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 15000,
        .buffer_size = HTTP_BUFFER_SIZE,
        .buffer_size_tx = 1024,
        .disable_auto_redirect = false,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t result = esp_http_client_open(client, 0);

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "HTTP-Verbindung fehlgeschlagen: %s",
            esp_err_to_name(result)
        );

        esp_http_client_cleanup(client);
        return result;
    }

    const int64_t content_length =
        esp_http_client_fetch_headers(client);

    const int status_code =
        esp_http_client_get_status_code(client);

    ESP_LOGI(TAG, "HTTP-Status: %d", status_code);
    ESP_LOGI(TAG, "Content-Length: %lld", content_length);

    if (status_code != 200) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    wav_stream_parser_t *parser = heap_caps_calloc(
        1,
        sizeof(wav_stream_parser_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (parser == NULL) {
        ESP_LOGE(TAG, "WAV-Parser konnte nicht im PSRAM angelegt werden");

        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    uint8_t *http_buffer = heap_caps_malloc(
        HTTP_BUFFER_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (http_buffer == NULL) {
        ESP_LOGE(TAG, "HTTP-Puffer konnte nicht im PSRAM angelegt werden");

        free(parser);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    wav_parser_init(parser);

    while (parser->state != WAV_STATE_FINISHED) {
        const int bytes_read = esp_http_client_read(
            client,
            (char *)http_buffer,
            HTTP_BUFFER_SIZE
        );

        if (bytes_read < 0) {
            ESP_LOGE(TAG, "HTTP-Lesefehler");
            result = ESP_FAIL;
            break;
        }

        if (bytes_read == 0) {
            ESP_LOGI(TAG, "HTTP-Stream beendet");
            break;
        }

        result = wav_parser_feed(
            parser,
            http_buffer,
            (size_t)bytes_read
        );

        if (result != ESP_OK) {
            break;
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (result == ESP_OK &&
        (!parser->format_found || !parser->data_found)) {

        ESP_LOGE(TAG, "WAV-Header unvollstaendig");
        result = ESP_ERR_INVALID_RESPONSE;
    }

    if (result == ESP_OK &&
        parser->pending_pcm_length != 0) {

        ESP_LOGW(
            TAG,
            "%u unvollstaendige PCM-Restbytes verworfen",
            (unsigned int)parser->pending_pcm_length
        );
    }

    if (result == ESP_OK) {
        ESP_LOGI(
            TAG,
            "PCM empfangen: %llu Bytes",
            parser->total_pcm_bytes
        );

        result = audio_flush(15000);

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Audiopuffer wurde nicht rechtzeitig geleert"
            );
        }
    }

    free(http_buffer);
    free(parser);

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "WAV-Streaming abgeschlossen");
    }

    return result;
}
esp_err_t streamer_play_mp3_stream(const char *url)
{
    if (url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Oeffne MP3-Stream: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 15000,
        .buffer_size = HTTP_BUFFER_SIZE,
        .buffer_size_tx = 1024,
        .disable_auto_redirect = false,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t result = esp_http_client_open(client, 0);

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "HTTP-Verbindung fehlgeschlagen: %s",
            esp_err_to_name(result)
        );

        esp_http_client_cleanup(client);
        return result;
    }

    const int64_t content_length =
        esp_http_client_fetch_headers(client);

    const int status_code =
        esp_http_client_get_status_code(client);

    ESP_LOGI(TAG, "HTTP-Status: %d", status_code);
    ESP_LOGI(TAG, "Content-Length: %lld", content_length);

    if (status_code != 200) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    uint8_t *http_buffer = heap_caps_malloc(
        HTTP_BUFFER_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (http_buffer == NULL) {
        ESP_LOGE(TAG, "HTTP-Puffer konnte nicht angelegt werden");

        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    result = mp3_decoder_init();

    if (result != ESP_OK) {
        free(http_buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return result;
    }
audio_set_playback_active(true);
    size_t total_received = 0;

    while (true) {
        const int bytes_read = esp_http_client_read(
            client,
            (char *)http_buffer,
            HTTP_BUFFER_SIZE
        );

        if (bytes_read < 0) {
            ESP_LOGE(TAG, "HTTP-Lesefehler");
            result = ESP_FAIL;
            break;
        }

        if (bytes_read == 0) {
            ESP_LOGI(TAG, "MP3-HTTP-Stream beendet");
            break;
        }

        total_received += (size_t)bytes_read;

        result = mp3_decoder_feed(
            http_buffer,
            (size_t)bytes_read
        );

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "MP3-Decodierung fehlgeschlagen: %s",
                esp_err_to_name(result)
            );
            break;
        }
    }

    if (result == ESP_OK) {
        result = mp3_decoder_finish();
    }

    mp3_decoder_deinit();
audio_set_playback_active(false);
    free(http_buffer);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(
        TAG,
        "MP3-Empfang beendet: %u Bytes",
        (unsigned int)total_received
    );

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "MP3-Streaming erfolgreich abgeschlossen");
    }

    return result;
}
esp_err_t streamer_play_mp3_radio(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t *http_buffer =
        heap_caps_malloc(
            HTTP_BUFFER_SIZE,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (http_buffer == NULL) {
        ESP_LOGE(
            TAG,
            "Radio-HTTP-Puffer konnte nicht angelegt werden"
        );

        return ESP_ERR_NO_MEM;
    }

    char current_url[RADIO_URL_BUFFER_SIZE];

    const int url_length = snprintf(
        current_url,
        sizeof(current_url),
        "%s",
        url
    );

    if (url_length <= 0 ||
        (size_t)url_length >= sizeof(current_url)) {

        free(http_buffer);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t final_result = ESP_OK;

    while (!streamer_radio_stop_requested()) {
        bool stream_started = false;

        for (int redirect_count = 0;
             redirect_count <= RADIO_MAX_REDIRECTS;
             redirect_count++) {

            if (streamer_radio_stop_requested()) {
                final_result = ESP_ERR_INVALID_STATE;
                break;
            }

            radio_http_context_t http_context = {0};

            ESP_LOGI(
                TAG,
                "Verbinde mit Webradio: %s",
                current_url
            );

            esp_http_client_config_t config = {
                .url = current_url,
                .timeout_ms = RADIO_HTTP_TIMEOUT_MS,
                .buffer_size = HTTP_BUFFER_SIZE,
                .buffer_size_tx = 1024,
                .disable_auto_redirect = true,
                .user_agent = "OpenAudio32/1.0",
                .keep_alive_enable = true,
                .crt_bundle_attach = esp_crt_bundle_attach,
                .event_handler = radio_http_event_handler,
                .user_data = &http_context,
            };

            esp_http_client_handle_t client =
                esp_http_client_init(&config);

            if (client == NULL) {
                final_result = ESP_ERR_NO_MEM;
                break;
            }

            esp_http_client_set_header(
                client,
                "Icy-MetaData",
                "0"
            );

            esp_err_t result =
                esp_http_client_open(
                    client,
                    0
                );

            if (result != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Radio-Verbindung fehlgeschlagen: %s",
                    esp_err_to_name(result)
                );

                esp_http_client_cleanup(client);
                final_result = result;
                break;
            }

            const int64_t content_length =
                esp_http_client_fetch_headers(client);

            const int status_code =
                esp_http_client_get_status_code(client);

            ESP_LOGI(
                TAG,
                "Radio HTTP-Status: %d",
                status_code
            );

            ESP_LOGI(
                TAG,
                "Radio Content-Length: %lld",
                content_length
            );

            /*
             * HTTP-Weiterleitung manuell behandeln.
             */
            if (status_code == 301 ||
                status_code == 302 ||
                status_code == 303 ||
                status_code == 307 ||
                status_code == 308) {

                if (http_context.location[0] == '\0') {
                    ESP_LOGE(
                        TAG,
                        "Redirect ohne Location-Header"
                    );

                    esp_http_client_close(client);
                    esp_http_client_cleanup(client);

                    final_result =
                        ESP_ERR_INVALID_RESPONSE;

                    break;
                }

                const int redirect_length =
                    snprintf(
                        current_url,
                        sizeof(current_url),
                        "%s",
                        http_context.location
                    );

                esp_http_client_close(client);
                esp_http_client_cleanup(client);

                if (redirect_length <= 0 ||
                    (size_t)redirect_length >=
                        sizeof(current_url)) {

                    ESP_LOGE(
                        TAG,
                        "Redirect-URL ist zu lang"
                    );

                    final_result =
                        ESP_ERR_INVALID_SIZE;

                    break;
                }

                ESP_LOGI(
                    TAG,
                    "Folge Redirect %d von %d",
                    redirect_count + 1,
                    RADIO_MAX_REDIRECTS
                );

                continue;
            }

            if (status_code != 200) {
                ESP_LOGE(
                    TAG,
                    "Radio liefert unerwarteten HTTP-Status: %d",
                    status_code
                );

                esp_http_client_close(client);
                esp_http_client_cleanup(client);

                final_result =
                    ESP_ERR_INVALID_RESPONSE;

                break;
            }

            result = mp3_decoder_init();

            if (result != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "MP3-Decoder konnte nicht gestartet werden: %s",
                    esp_err_to_name(result)
                );

                esp_http_client_close(client);
                esp_http_client_cleanup(client);

                final_result = result;
                break;
            }

            /*
             * Erst jetzt gilt die Wiedergabe als aktiv.
             * Vorher dürfen keine Unterläufe gezählt werden.
             */
            audio_set_playback_active(true);

            ESP_LOGI(
                TAG,
                "Webradio-Stream gestartet"
            );

            size_t total_received = 0;
            stream_started = true;

            while (!streamer_radio_stop_requested()) {
                const int bytes_read =
                    esp_http_client_read(
                        client,
                        (char *)http_buffer,
                        HTTP_BUFFER_SIZE
                    );

                if (streamer_radio_stop_requested()) {
                    result = ESP_ERR_INVALID_STATE;
                    break;
                }

                if (bytes_read > 0) {
                    total_received +=
                        (size_t)bytes_read;

                    result = mp3_decoder_feed(
                        http_buffer,
                        (size_t)bytes_read
                    );

                    if (result != ESP_OK) {
                        ESP_LOGE(
                            TAG,
                            "Radio-MP3-Decodierung fehlgeschlagen: %s",
                            esp_err_to_name(result)
                        );

                        break;
                    }

                    continue;
                }

                if (bytes_read == 0) {
                    ESP_LOGW(
                        TAG,
                        "Radioverbindung wurde beendet"
                    );

                    result = ESP_ERR_TIMEOUT;
                    break;
                }

                ESP_LOGW(
                    TAG,
                    "Radio-HTTP-Lesefehler"
                );

                result = ESP_FAIL;
                break;
            }

            audio_set_playback_active(false);
            mp3_decoder_deinit();

            esp_http_client_close(client);
            esp_http_client_cleanup(client);

            ESP_LOGI(
                TAG,
                "Radioverbindung geschlossen, empfangen: %u Bytes",
                (unsigned int)total_received
            );

            if (streamer_radio_stop_requested()) {
                final_result = ESP_ERR_INVALID_STATE;
            } else {
                final_result = result;
            }

            break;
        }

        if (streamer_radio_stop_requested()) {
            final_result = ESP_ERR_INVALID_STATE;
            break;
        }

        if (!stream_started) {
            audio_set_playback_active(false);
        }

        ESP_LOGI(
            TAG,
            "Neuer Verbindungsversuch in %d ms",
            RADIO_RECONNECT_DELAY_MS
        );

        for (int elapsed_ms = 0;
             elapsed_ms < RADIO_RECONNECT_DELAY_MS;
             elapsed_ms += 100) {

            if (streamer_radio_stop_requested()) {
                break;
            }

            vTaskDelay(
                pdMS_TO_TICKS(100)
            );
        }

        /*
         * Beim nächsten kompletten Versuch wieder mit der
         * ursprünglichen Senderadresse beginnen.
         */
        snprintf(
            current_url,
            sizeof(current_url),
            "%s",
            url
        );
    }

    audio_set_playback_active(false);
    free(http_buffer);

    ESP_LOGI(
        TAG,
        "Webradio-Streaming beendet"
    );

    return final_result;
}