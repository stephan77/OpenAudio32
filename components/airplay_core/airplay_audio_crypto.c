#include <string.h>

#include "audio_crypto.h"

#include "mbedtls/aes.h"
//#include "sodium.h"

int audio_crypto_decrypt_rtp(const audio_encrypt_t *encrypt,
                             const uint8_t *input, size_t input_len,
                             uint8_t *output, size_t output_capacity,
                             const uint8_t *full_packet,
                             size_t full_packet_len) {
  if (!encrypt || !input || !output) {
    return -1;
  }

  if (encrypt->type == AUDIO_ENCRYPT_NONE) {
    if (input_len > output_capacity) {
      return -1;
    }
    memcpy(output, input, input_len);
    return (int)input_len;
  }

  if (encrypt->type == AUDIO_ENCRYPT_AES_CBC) {
    if (input_len > output_capacity) {
      return -1;
    }

    uint8_t iv[16];
    memcpy(iv, encrypt->iv, sizeof(iv));

    size_t num_blocks = input_len / 16;
    size_t remainder = input_len % 16;
    size_t encrypted_len = num_blocks * 16;

    if (encrypted_len > 0) {
      mbedtls_aes_context aes;
      mbedtls_aes_init(&aes);

      int ret = mbedtls_aes_setkey_dec(&aes, encrypt->key, 128);
      if (ret != 0) {
        mbedtls_aes_free(&aes);
        return -1;
      }

      ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, encrypted_len, iv,
                                  input, output);
      mbedtls_aes_free(&aes);
      if (ret != 0) {
        return -1;
      }
    }

    if (remainder > 0) {
      memcpy(output + encrypted_len, input + encrypted_len, remainder);
    }

    return (int)input_len;
  }

    return -1;
}

int audio_crypto_decrypt_buffered(
    const audio_encrypt_t *encrypt,
    const uint8_t *packet,
    size_t packet_len,
    uint8_t *output,
    size_t output_capacity
)
{
    if (packet == NULL || output == NULL) {
        return -1;
    }

    if (encrypt != NULL &&
        encrypt->type == AUDIO_ENCRYPT_CHACHA20_POLY1305) {
        return -1;
    }

    if (packet_len <= 12U) {
        return -1;
    }

    const size_t payload_len =
        packet_len - 12U;

    if (payload_len > output_capacity) {
        return -1;
    }

    memcpy(
        output,
        packet + 12U,
        payload_len
    );

    return (int)payload_len;
}