#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#include "audio_decoder.h"

#include "esp_log.h"

#include "alac_magic_cookie.h"
//#include "decoder/impl/esp_aac_dec.h"
#include "decoder/impl/esp_alac_dec.h"
#include "esp_audio_dec.h"

//#define ADTS_HEADER_LEN       7
#define MAX_FALLBACK_CHANNELS 2

typedef enum {
  AUDIO_DECODER_NONE = 0,
  AUDIO_DECODER_PCM,
  AUDIO_DECODER_ALAC
} audio_decoder_kind_t;

struct audio_decoder {
  audio_decoder_kind_t kind;
  audio_format_t format;
  void *alac_decoder;
  uint8_t alac_magic_cookie[ALAC_MAGIC_COOKIE_SIZE];
};

static const char *TAG = "audio_dec";

// Reopen the AAC decoder to reset its internal state after a corrupt frame.
// The codec's state machine can get stuck after certain errors (e.g. error 20)
// and will continue failing every subsequent frame until it is recreated.


static bool codec_is_alac(const char *codec) {
  if (!codec) {
    return false;
  }
  return strcmp(codec, "AppleLossless") == 0 || strcmp(codec, "ALAC") == 0;
}


audio_decoder_t *audio_decoder_create(const audio_decoder_config_t *config) {
  if (!config) {
    return NULL;
  }

  audio_decoder_t *decoder = calloc(1, sizeof(*decoder));
  if (!decoder) {
    return NULL;
  }

  decoder->format = config->format;

  if (codec_is_alac(config->format.codec)) {
    decoder->kind = AUDIO_DECODER_ALAC;
    build_alac_magic_cookie(decoder->alac_magic_cookie, &config->format);

    esp_alac_dec_cfg_t alac_cfg = {.codec_spec_info =
                                       decoder->alac_magic_cookie,
                                   .spec_info_len = ALAC_MAGIC_COOKIE_SIZE};

    esp_audio_err_t err =
        esp_alac_dec_open(&alac_cfg, sizeof(alac_cfg), &decoder->alac_decoder);
    if (err != ESP_AUDIO_ERR_OK) {
      ESP_LOGE(TAG, "Failed to open ALAC decoder: %d", err);
      decoder->alac_decoder = NULL;
      decoder->kind = AUDIO_DECODER_NONE;
    }
  
  } else if (strcmp(config->format.codec, "L16") == 0 ||
             strcmp(config->format.codec, "PCM") == 0) {
    decoder->kind = AUDIO_DECODER_PCM;
  } else {
    decoder->kind = AUDIO_DECODER_NONE;
  }

  return decoder;
}

void audio_decoder_destroy(audio_decoder_t *decoder) {
  if (!decoder) {
    return;
  }

  if (decoder->alac_decoder) {
    esp_alac_dec_close(decoder->alac_decoder);
    decoder->alac_decoder = NULL;
  }

  free(decoder);
}

int audio_decoder_decode(audio_decoder_t *decoder, const uint8_t *input,
                         size_t input_len, int16_t *output,
                         size_t output_capacity_samples,
                         audio_decode_info_t *info) {
  if (!decoder || !input || !output || output_capacity_samples == 0) {
    return -1;
  }

  int channels = decoder->format.channels;
  if (channels <= 0) {
    channels = MAX_FALLBACK_CHANNELS;
  }

  if (decoder->kind == AUDIO_DECODER_PCM) {
    size_t decoded_samples = input_len / (channels * sizeof(int16_t));
    if (decoded_samples > output_capacity_samples) {
      decoded_samples = output_capacity_samples;
    }

    const int16_t *src = (const int16_t *)input;
    for (size_t i = 0; i < decoded_samples * channels; i++) {
      output[i] = ntohs(src[i]);
    }

    if (info) {
      info->channels = channels;
    }
    return (int)decoded_samples;
  }

  if (decoder->kind == AUDIO_DECODER_ALAC) {
    if (!decoder->alac_decoder) {
      return -1;
    }

    esp_audio_dec_in_raw_t raw = {.buffer = (uint8_t *)input,
                                  .len = (uint32_t)input_len,
                                  .consumed = 0,
                                  .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE};
    esp_audio_dec_out_frame_t frame = {
        .buffer = (uint8_t *)output,
        .len = (uint32_t)(output_capacity_samples * channels * sizeof(int16_t)),
        .decoded_size = 0};
    esp_audio_dec_info_t dec_info = {0};

    esp_audio_err_t err =
        esp_alac_dec_decode(decoder->alac_decoder, &raw, &frame, &dec_info);
    if (err != ESP_AUDIO_ERR_OK) {
      return -1;
    }

    int dec_channels = dec_info.channel > 0 ? dec_info.channel : channels;
    if (dec_channels <= 0) {
      dec_channels = MAX_FALLBACK_CHANNELS;
    }

    size_t decoded_samples =
        frame.decoded_size / (dec_channels * sizeof(int16_t));
    if (decoded_samples > output_capacity_samples) {
      decoded_samples = output_capacity_samples;
    }

    if (info) {
      info->channels = dec_channels;
    }
    return (int)decoded_samples;
  }
  return -1;

}
bool audio_decoder_is_aac(
    const audio_decoder_t *decoder
)
{
    /*
     * OpenAudio32 unterstützt in der aktuellen
     * AirPlay-v1-Integration nur PCM und ALAC.
     */
    (void)decoder;
    return false;
}
bool audio_decoder_is_alac(const audio_decoder_t *decoder) {
  return decoder && decoder->kind == AUDIO_DECODER_ALAC;
}
