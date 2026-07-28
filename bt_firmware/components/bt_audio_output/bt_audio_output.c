#include "bt_audio_output.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"

#include "freertos/task.h"

static const char *TAG = "bt_audio_output";

#define BT_I2S_PORT        I2S_NUM_0
#define BT_I2S_BCLK_GPIO   GPIO_NUM_26
#define BT_I2S_WS_GPIO     GPIO_NUM_25
#define BT_I2S_DATA_GPIO   GPIO_NUM_22

#define BT_I2S_DEFAULT_SAMPLE_RATE 44100U

static i2s_chan_handle_t s_tx_channel = NULL;
static uint32_t s_sample_rate_hz =
    BT_I2S_DEFAULT_SAMPLE_RATE;

esp_err_t bt_audio_output_init(void)
{
    if (s_tx_channel != NULL) {
        return ESP_OK;
    }

    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(
            BT_I2S_PORT,
            I2S_ROLE_MASTER
        );

    channel_config.dma_desc_num = 8;
    channel_config.dma_frame_num = 256;
    channel_config.auto_clear = true;

    esp_err_t result =
        i2s_new_channel(
            &channel_config,
            &s_tx_channel,
            NULL
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2S-TX-Kanal konnte nicht erstellt werden: %s",
            esp_err_to_name(result)
        );

        s_tx_channel = NULL;
        return result;
    }

    i2s_std_config_t standard_config = {
        .clk_cfg =
            I2S_STD_CLK_DEFAULT_CONFIG(
                s_sample_rate_hz
            ),

        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_16BIT,
                I2S_SLOT_MODE_STEREO
            ),

        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BT_I2S_BCLK_GPIO,
            .ws = BT_I2S_WS_GPIO,
            .dout = BT_I2S_DATA_GPIO,
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
            s_tx_channel,
            &standard_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2S-Standardmodus konnte nicht initialisiert werden: %s",
            esp_err_to_name(result)
        );

        i2s_del_channel(s_tx_channel);
        s_tx_channel = NULL;

        return result;
    }

    result =
        i2s_channel_enable(
            s_tx_channel
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2S-TX konnte nicht aktiviert werden: %s",
            esp_err_to_name(result)
        );

        i2s_del_channel(s_tx_channel);
        s_tx_channel = NULL;

        return result;
    }

    ESP_LOGI(
        TAG,
        "I2S-TX gestartet: BCLK=%d, WS=%d, DATA=%d, %lu Hz",
        BT_I2S_BCLK_GPIO,
        BT_I2S_WS_GPIO,
        BT_I2S_DATA_GPIO,
        (unsigned long)s_sample_rate_hz
    );

    return ESP_OK;
}

esp_err_t bt_audio_output_set_sample_rate(
    uint32_t sample_rate_hz
)
{
    if (s_tx_channel == NULL ||
        sample_rate_hz < 8000U ||
        sample_rate_hz > 96000U) {

        return ESP_ERR_INVALID_ARG;
    }

    if (sample_rate_hz == s_sample_rate_hz) {
        return ESP_OK;
    }

    esp_err_t result =
        i2s_channel_disable(
            s_tx_channel
        );

    if (result != ESP_OK) {
        return result;
    }

    i2s_std_clk_config_t clock_config =
        I2S_STD_CLK_DEFAULT_CONFIG(
            sample_rate_hz
        );

    result =
        i2s_channel_reconfig_std_clock(
            s_tx_channel,
            &clock_config
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "I2S-Samplerate konnte nicht geändert werden: %s",
            esp_err_to_name(result)
        );

        i2s_channel_enable(s_tx_channel);
        return result;
    }

    result =
        i2s_channel_enable(
            s_tx_channel
        );

    if (result != ESP_OK) {
        return result;
    }

    s_sample_rate_hz =
        sample_rate_hz;

    ESP_LOGI(
        TAG,
        "I2S-Samplerate jetzt %lu Hz",
        (unsigned long)s_sample_rate_hz
    );

    return ESP_OK;
}

esp_err_t bt_audio_output_write(
    const void *data,
    size_t data_size
)
{
    if (s_tx_channel == NULL ||
        data == NULL ||
        data_size == 0U) {

        return ESP_ERR_INVALID_ARG;
    }

    size_t bytes_written = 0;

    const esp_err_t result =
        i2s_channel_write(
            s_tx_channel,
            data,
            data_size,
            &bytes_written,
            portMAX_DELAY
        );

    if (result != ESP_OK) {
        return result;
    }

    return bytes_written == data_size
        ? ESP_OK
        : ESP_ERR_INVALID_SIZE;
}