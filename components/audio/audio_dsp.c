#include "audio_dsp.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#define PI_F 3.14159265358979323846f
static const char *TAG =
    "audio_dsp";

typedef struct {
    bool initialized;

    uint32_t sample_rate;
    uint8_t channels;

    audio_dsp_settings_t settings;

    /*
     * Vorberechnete lineare Verstärkungswerte.
     *
     * Damit muss im Audio-Hotpath nicht für jedes Sample
     * powf() aufgerufen werden.
     */
    float left_gain;
    float right_gain;

    SemaphoreHandle_t mutex;
} audio_dsp_context_t;

static audio_dsp_context_t dsp = {
    .initialized = false,
    .sample_rate = 44100,
    .channels = AUDIO_DSP_CHANNELS,
    .settings = {
        .enabled = true,
        .muted = false,
        .gain_db = 0.0f,
        .balance = 0.0f,
        .bass_db = 0.0f,
        .treble_db = 0.0f,
        .eq = {
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f
        },
        .loudness_enabled = false,
        .limiter_enabled = true,
        .limiter_threshold_db = -1.0f,
    },
    .left_gain = 1.0f,
    .right_gain = 1.0f,
    .mutex = NULL,
};
static audio_biquad_t bass_filter;
static audio_biquad_t treble_filter;
static float limiter_gain = 1.0f;
#define EQ_BANDS 10

static audio_biquad_t eq_filters[EQ_BANDS];
static const float eq_freq[EQ_BANDS] = {
    31.25f,
    62.5f,
    125.0f,
    250.0f,
    500.0f,
    1000.0f,
    2000.0f,
    4000.0f,
    8000.0f,
    16000.0f
};
static float clamp_float(
    float value,
    float minimum,
    float maximum
)
{
    if (value < minimum) {
        return minimum;
    }

    if (value > maximum) {
        return maximum;
    }

    return value;
}

static int16_t clamp_s16(
    float value
)
{
    if (value > 32767.0f) {
        return INT16_MAX;
    }

    if (value < -32768.0f) {
        return INT16_MIN;
    }

    /*
     * lrintf rundet sauber auf den nächsten Integerwert.
     */
    return (int16_t)lrintf(value);
}
static inline float process_limiter(
    float sample,
    float threshold,
    bool enabled
)
{
    if (!enabled) {
        return sample;
    }

    const float abs_sample = fabsf(sample);

    if (abs_sample > threshold) {

        const float target_gain =
            threshold / abs_sample;

        /* Attack */
        limiter_gain =
            limiter_gain * 0.95f +
            target_gain * 0.05f;

    } else {

        /* Release */
        limiter_gain =
            limiter_gain * 0.9995f +
            1.0f * 0.0005f;
    }

    if (limiter_gain > 1.0f) {
        limiter_gain = 1.0f;
    }

    return sample * limiter_gain;
}

static inline float process_biquad(
    audio_biquad_t *filter,
    float input,
    bool left
)
{
    float *z1 =
        left
            ? &filter->z1L
            : &filter->z1R;

    float *z2 =
        left
            ? &filter->z2L
            : &filter->z2R;

    float output =
        input * filter->b0 +
        *z1;

    *z1 =
        input * filter->b1 +
        *z2 -
        filter->a1 * output;

    *z2 =
        input * filter->b2 -
        filter->a2 * output;

    return output;
}
static void normalize_biquad(

    audio_biquad_t *filter,

    float b0,

    float b1,

    float b2,

    float a0,

    float a1,

    float a2

)

{

    filter->b0 = b0 / a0;

    filter->b1 = b1 / a0;

    filter->b2 = b2 / a0;

    filter->a1 = a1 / a0;

    filter->a2 = a2 / a0;

}
static void design_low_shelf(
    audio_biquad_t *filter,
    float gain_db,
    float sample_rate
)
{
    const float fc = 120.0f;

    const float A =
        powf(
            10.0f,
            gain_db / 40.0f
        );

    const float w =
        2.0f * PI_F * fc /
        sample_rate;

    const float cs =
        cosf(w);

    const float sn =
        sinf(w);

    const float alpha =
        sn / 2.0f *
        sqrtf(2.0f);

    const float beta =
        2.0f *
        sqrtf(A) *
        alpha;

    normalize_biquad(
        filter,

        A * (
            (A + 1) -
            (A - 1) * cs +
            beta
        ),

        2 * A * (
            (A - 1) -
            (A + 1) * cs
        ),

        A * (
            (A + 1) -
            (A - 1) * cs -
            beta
        ),

        (A + 1) +
        (A - 1) * cs +
        beta,

        -2 * (
            (A - 1) +
            (A + 1) * cs
        ),

        (A + 1) +
        (A - 1) * cs -
        beta
    );
}

static void design_high_shelf(
    audio_biquad_t *filter,
    float gain_db,
    float sample_rate
)
{
    const float fc = 6000.0f;

    const float A =
        powf(
            10.0f,
            gain_db / 40.0f
        );

    const float w =
        2.0f * PI_F * fc /
        sample_rate;

    const float cs =
        cosf(w);

    const float sn =
        sinf(w);

    const float alpha =
        sn / 2.0f *
        sqrtf(2.0f);

    const float beta =
        2.0f *
        sqrtf(A) *
        alpha;

    normalize_biquad(
        filter,

        A * (
            (A + 1) +
            (A - 1) * cs +
            beta
        ),

        -2 * A * (
            (A - 1) +
            (A + 1) * cs
        ),

        A * (
            (A + 1) +
            (A - 1) * cs -
            beta
        ),

        (A + 1) -
        (A - 1) * cs +
        beta,

        2 * (
            (A - 1) -
            (A + 1) * cs
        ),

        (A + 1) -
        (A - 1) * cs -
        beta
    );
}
static void design_peak(
    audio_biquad_t *filter,
    float gain_db,
    float fc,
    float sample_rate)
{
    const float Q = 1.4f;

    const float A =
        powf(10.0f, gain_db / 40.0f);

    const float w =
        2.0f * PI_F * fc / sample_rate;

    const float alpha =
        sinf(w) / (2.0f * Q);

    const float cs =
        cosf(w);

    normalize_biquad(
        filter,

        1 + alpha * A,
        -2 * cs,
        1 - alpha * A,

        1 + alpha / A,
        -2 * cs,
        1 - alpha / A
    );
}
static bool settings_are_valid(
    const audio_dsp_settings_t *settings
)
{
    if (settings == NULL) {
        return false;
    }

    if (!isfinite(settings->gain_db) ||
        settings->gain_db < AUDIO_DSP_MIN_GAIN_DB ||
        settings->gain_db > AUDIO_DSP_MAX_GAIN_DB) {

        return false;
    }

    if (!isfinite(settings->balance) ||
        settings->balance < AUDIO_DSP_MIN_BALANCE ||
        settings->balance > AUDIO_DSP_MAX_BALANCE) {

        return false;
    }

    if (!isfinite(settings->bass_db) ||
        settings->bass_db < AUDIO_DSP_MIN_TONE_DB ||
        settings->bass_db > AUDIO_DSP_MAX_TONE_DB) {

        return false;
    }

    if (!isfinite(settings->treble_db) ||
        settings->treble_db < AUDIO_DSP_MIN_TONE_DB ||
        settings->treble_db > AUDIO_DSP_MAX_TONE_DB) {

        return false;
    }

    if (!isfinite(settings->limiter_threshold_db) ||
        settings->limiter_threshold_db < -20.0f ||
        settings->limiter_threshold_db > 0.0f) {

        return false;
    }
for (size_t index = 0;
     index < EQ_BANDS;
     index++) {

    if (!isfinite(settings->eq[index]) ||
        settings->eq[index] < -12.0f ||
        settings->eq[index] > 12.0f) {

        return false;
    }
}
    return true;
}
static void reset_biquad_state(
    audio_biquad_t *filter
)
{
    if (filter == NULL) {
        return;
    }

    filter->z1L = 0.0f;
    filter->z2L = 0.0f;
    filter->z1R = 0.0f;
    filter->z2R = 0.0f;
}
static void redesign_filters_locked(void)
{
    design_low_shelf(
        &bass_filter,
        dsp.settings.bass_db,
        (float)dsp.sample_rate
    );

    design_high_shelf(
        &treble_filter,
        dsp.settings.treble_db,
        (float)dsp.sample_rate
    );

    reset_biquad_state(
        &bass_filter
    );

    reset_biquad_state(
        &treble_filter
    );

    for (size_t index = 0;
         index < EQ_BANDS;
         index++) {

        design_peak(
            &eq_filters[index],
            dsp.settings.eq[index],
            eq_freq[index],
            (float)dsp.sample_rate
        );

        reset_biquad_state(
            &eq_filters[index]
        );
    }
}
static void recalculate_channel_gains_locked(void)
{
    /*
     * Dezibel in linearen Faktor umrechnen:
     *
     * linear = 10^(dB / 20)
     */
    const float master_gain =
        powf(
            10.0f,
            dsp.settings.gain_db / 20.0f
        );

    const float balance =
        clamp_float(
            dsp.settings.balance,
            AUDIO_DSP_MIN_BALANCE,
            AUDIO_DSP_MAX_BALANCE
        );

    /*
     * Einfache Balance-Regelung:
     *
     * Balance negativ:
     *   rechter Kanal wird abgesenkt.
     *
     * Balance positiv:
     *   linker Kanal wird abgesenkt.
     *
     * Der bevorzugte Kanal wird nicht zusätzlich verstärkt.
     */
    float left_balance_gain = 1.0f;
    float right_balance_gain = 1.0f;

    if (balance < 0.0f) {
        right_balance_gain =
            1.0f + balance;
    } else if (balance > 0.0f) {
        left_balance_gain =
            1.0f - balance;
    }

    dsp.left_gain =
        master_gain *
        left_balance_gain;

    dsp.right_gain =
        master_gain *
        right_balance_gain;
}

static esp_err_t lock_dsp(
    TickType_t timeout
)
{
    if (dsp.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            dsp.mutex,
            timeout
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void unlock_dsp(void)
{
    if (dsp.mutex != NULL) {
        xSemaphoreGive(
            dsp.mutex
        );
    }
}

esp_err_t audio_dsp_init(
    uint32_t sample_rate,
    uint8_t channels
)
{
    if (sample_rate < 8000U ||
        sample_rate > 192000U) {

        return ESP_ERR_INVALID_ARG;
    }

    if (channels != AUDIO_DSP_CHANNELS) {
        ESP_LOGE(
            TAG,
            "Nur Stereo wird unterstützt, Kanäle=%u",
            (unsigned int)channels
        );

        return ESP_ERR_NOT_SUPPORTED;
    }

    if (dsp.mutex == NULL) {
        dsp.mutex =
            xSemaphoreCreateMutex();

        if (dsp.mutex == NULL) {
            ESP_LOGE(
                TAG,
                "DSP-Mutex konnte nicht erstellt werden"
            );

            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t result =
        lock_dsp(
            portMAX_DELAY
        );

    if (result != ESP_OK) {
        return result;
    }

 dsp.sample_rate =
    sample_rate;

dsp.channels =
    channels;

redesign_filters_locked();

    dsp.initialized =
        true;

    recalculate_channel_gains_locked();

    unlock_dsp();

    ESP_LOGI(
        TAG,
        "DSP initialisiert: %u Hz, %u Kanäle",
        (unsigned int)sample_rate,
        (unsigned int)channels
    );

    return ESP_OK;
}

void audio_dsp_deinit(void)
{
    if (dsp.mutex != NULL) {
        if (lock_dsp(
                portMAX_DELAY
            ) == ESP_OK) {

            dsp.initialized =
                false;

            unlock_dsp();
        }

        vSemaphoreDelete(
            dsp.mutex
        );

        dsp.mutex =
            NULL;
    }

    ESP_LOGI(
        TAG,
        "DSP beendet"
    );
}

esp_err_t audio_dsp_set_sample_rate(
    uint32_t sample_rate
)
{
    if (sample_rate < 8000U ||
        sample_rate > 192000U) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        lock_dsp(
            pdMS_TO_TICKS(100)
        );

    if (result != ESP_OK) {
        return result;
    }

    dsp.sample_rate =
        sample_rate;
    redesign_filters_locked();
    unlock_dsp();

    ESP_LOGI(
        TAG,
        "DSP-Samplerate auf %u Hz gesetzt",
        (unsigned int)sample_rate
    );

    return ESP_OK;
}

uint32_t audio_dsp_get_sample_rate(void)
{
    uint32_t result =
        dsp.sample_rate;

    if (lock_dsp(
            pdMS_TO_TICKS(20)
        ) == ESP_OK) {

        result =
            dsp.sample_rate;

        unlock_dsp();
    }

    return result;
}

esp_err_t audio_dsp_set_settings(
    const audio_dsp_settings_t *settings
)
{
    if (!settings_are_valid(settings)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        lock_dsp(
            pdMS_TO_TICKS(100)
        );

    if (result != ESP_OK) {
        return result;
    }

    dsp.settings =
        *settings;

    recalculate_channel_gains_locked();
    redesign_filters_locked();

    unlock_dsp();

    ESP_LOGI(
        TAG,
        "DSP-Einstellungen übernommen"
    );

    return ESP_OK;
}

esp_err_t audio_dsp_get_settings(
    audio_dsp_settings_t *settings
)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        lock_dsp(
            pdMS_TO_TICKS(100)
        );

    if (result != ESP_OK) {
        return result;
    }

    *settings =
        dsp.settings;

    unlock_dsp();

    return ESP_OK;
}

esp_err_t audio_dsp_reset_settings(void)
{
    const audio_dsp_settings_t defaults = {
        .enabled = true,
        .muted = false,
        .gain_db = 0.0f,
        .balance = 0.0f,
        .bass_db = 0.0f,
        .treble_db = 0.0f,
        .eq = {
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f
        },
        .loudness_enabled = false,
        .limiter_enabled = true,
        .limiter_threshold_db = -1.0f,
    };

    return audio_dsp_set_settings(
        &defaults
    );
}

esp_err_t audio_dsp_set_enabled(
    bool enabled
)
{
    esp_err_t result =
        lock_dsp(
            pdMS_TO_TICKS(100)
        );

    if (result != ESP_OK) {
        return result;
    }

    dsp.settings.enabled =
        enabled;

    unlock_dsp();

    return ESP_OK;
}

bool audio_dsp_is_enabled(void)
{
    bool enabled =
        dsp.settings.enabled;

    if (lock_dsp(
            pdMS_TO_TICKS(20)
        ) == ESP_OK) {

        enabled =
            dsp.settings.enabled;

        unlock_dsp();
    }

    return enabled;
}

esp_err_t audio_dsp_set_mute(
    bool muted
)
{
    esp_err_t result =
        lock_dsp(
            pdMS_TO_TICKS(100)
        );

    if (result != ESP_OK) {
        return result;
    }

    dsp.settings.muted =
        muted;

    unlock_dsp();

    return ESP_OK;
}

bool audio_dsp_is_muted(void)
{
    bool muted =
        dsp.settings.muted;

    if (lock_dsp(
            pdMS_TO_TICKS(20)
        ) == ESP_OK) {

        muted =
            dsp.settings.muted;

        unlock_dsp();
    }

    return muted;
}

esp_err_t audio_dsp_set_gain_db(
    float gain_db
)
{
    if (!isfinite(gain_db) ||
        gain_db < AUDIO_DSP_MIN_GAIN_DB ||
        gain_db > AUDIO_DSP_MAX_GAIN_DB) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        lock_dsp(
            pdMS_TO_TICKS(100)
        );

    if (result != ESP_OK) {
        return result;
    }

    dsp.settings.gain_db =
        gain_db;

    recalculate_channel_gains_locked();

    unlock_dsp();

    return ESP_OK;
}

float audio_dsp_get_gain_db(void)
{
    float gain_db =
        dsp.settings.gain_db;

    if (lock_dsp(
            pdMS_TO_TICKS(20)
        ) == ESP_OK) {

        gain_db =
            dsp.settings.gain_db;

        unlock_dsp();
    }

    return gain_db;
}

esp_err_t audio_dsp_set_balance(
    float balance
)
{
    if (!isfinite(balance) ||
        balance < AUDIO_DSP_MIN_BALANCE ||
        balance > AUDIO_DSP_MAX_BALANCE) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        lock_dsp(
            pdMS_TO_TICKS(100)
        );

    if (result != ESP_OK) {
        return result;
    }

    dsp.settings.balance =
        balance;

    recalculate_channel_gains_locked();

    unlock_dsp();

    return ESP_OK;
}

float audio_dsp_get_balance(void)
{
    float balance =
        dsp.settings.balance;

    if (lock_dsp(
            pdMS_TO_TICKS(20)
        ) == ESP_OK) {

        balance =
            dsp.settings.balance;

        unlock_dsp();
    }

    return balance;
}

esp_err_t audio_dsp_set_bass_db(
    float bass_db
)
{
    if (!isfinite(bass_db) ||
        bass_db < AUDIO_DSP_MIN_TONE_DB ||
        bass_db > AUDIO_DSP_MAX_TONE_DB) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        lock_dsp(
            pdMS_TO_TICKS(100)
        );

    if (result != ESP_OK) {
        return result;
    }

    dsp.settings.bass_db =
        bass_db;

    design_low_shelf(
        &bass_filter,
        bass_db,
        (float)dsp.sample_rate
    );

    reset_biquad_state(
        &bass_filter
    );

    unlock_dsp();

    return ESP_OK;
}

float audio_dsp_get_bass_db(void)
{
    float bass_db =
        dsp.settings.bass_db;

    if (lock_dsp(
            pdMS_TO_TICKS(20)
        ) == ESP_OK) {

        bass_db =
            dsp.settings.bass_db;

        unlock_dsp();
    }

    return bass_db;
}

esp_err_t audio_dsp_set_treble_db(
    float treble_db
)
{
    if (!isfinite(treble_db) ||
        treble_db < AUDIO_DSP_MIN_TONE_DB ||
        treble_db > AUDIO_DSP_MAX_TONE_DB) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        lock_dsp(
            pdMS_TO_TICKS(100)
        );

    if (result != ESP_OK) {
        return result;
    }

    dsp.settings.treble_db =
        treble_db;

    design_high_shelf(
        &treble_filter,
        treble_db,
        (float)dsp.sample_rate
    );

    reset_biquad_state(
        &treble_filter
    );

    unlock_dsp();

    return ESP_OK;
}
esp_err_t audio_dsp_set_eq_band(
    size_t band_index,
    float gain_db
)
{
    if (band_index >= EQ_BANDS ||
        !isfinite(gain_db) ||
        gain_db < -12.0f ||
        gain_db > 12.0f) {

        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result =
        lock_dsp(
            pdMS_TO_TICKS(100)
        );

    if (result != ESP_OK) {
        return result;
    }

    dsp.settings.eq[band_index] =
        gain_db;

    design_peak(
        &eq_filters[band_index],
        gain_db,
        eq_freq[band_index],
        (float)dsp.sample_rate
    );

    reset_biquad_state(
        &eq_filters[band_index]
    );

    unlock_dsp();

    return ESP_OK;
}
float audio_dsp_get_treble_db(void)
{
    float treble_db =
        dsp.settings.treble_db;

    if (lock_dsp(
            pdMS_TO_TICKS(20)
        ) == ESP_OK) {

        treble_db =
            dsp.settings.treble_db;

        unlock_dsp();
    }

    return treble_db;
}

esp_err_t audio_dsp_process_s16(
    int16_t *samples,
    size_t frame_count
)
{
    if (samples == NULL &&
        frame_count > 0U) {

        return ESP_ERR_INVALID_ARG;
    }

    if (frame_count == 0U) {
        return ESP_OK;
    }

    /*
     * Nur sehr kurz sperren und die benötigten Werte lokal kopieren.
     * Die komplette Sample-Verarbeitung darf nicht unter dem Mutex
     * stattfinden, sonst würden Webanfragen die Audioausgabe blockieren.
     */
    bool initialized =
        dsp.initialized;

    bool enabled =
        dsp.settings.enabled;

    bool muted =
        dsp.settings.muted;

    float left_gain =
        dsp.left_gain;

    float right_gain =
        dsp.right_gain;
    bool limiter_enabled =
        dsp.settings.limiter_enabled;

float limiter_threshold =
    32767.0f *
    powf(
        10.0f,
        dsp.settings.limiter_threshold_db / 20.0f
    );

    if (lock_dsp(
            pdMS_TO_TICKS(5)
        ) == ESP_OK) {

        initialized =
            dsp.initialized;

        enabled =
            dsp.settings.enabled;

        muted =
            dsp.settings.muted;

        left_gain =
            dsp.left_gain;

        right_gain =
            dsp.right_gain;
limiter_enabled =
    dsp.settings.limiter_enabled;

limiter_threshold =
    32767.0f *
    powf(
        10.0f,
        dsp.settings.limiter_threshold_db / 20.0f
    );
        unlock_dsp();
    }

    if (!initialized ||
        !enabled) {

        return ESP_OK;
    }

    if (muted) {
        memset(
            samples,
            0,
            frame_count *
                AUDIO_DSP_CHANNELS *
                sizeof(int16_t)
        );

        return ESP_OK;
    }

    /*
     * Interleaved Stereo:
     *
     * samples[0] = links
     * samples[1] = rechts
     * samples[2] = links
     * samples[3] = rechts
     */
 for (size_t frame = 0;
     frame < frame_count;
     frame++) {

    const size_t left_index =
        frame * AUDIO_DSP_CHANNELS;

    const size_t right_index =
        left_index + 1U;

    float left =
        (float)samples[left_index];

    float right =
        (float)samples[right_index];

    left = process_biquad(
        &bass_filter,
        left,
        true
    );

    right = process_biquad(
        &bass_filter,
        right,
        false
    );

    left = process_biquad(
        &treble_filter,
        left,
        true
    );

    right = process_biquad(
        &treble_filter,
        right,
        false
    );

    for (size_t index = 0;
         index < EQ_BANDS;
         index++) {

        left = process_biquad(
            &eq_filters[index],
            left,
            true
        );

        right = process_biquad(
            &eq_filters[index],
            right,
            false
        );
    }

    left *= left_gain;
    right *= right_gain;
left =
    process_limiter(
        left,
        limiter_threshold,
        limiter_enabled
    );

right =
    process_limiter(
        right,
        limiter_threshold,
        limiter_enabled
    );
    samples[left_index] =
        clamp_s16(left);

    samples[right_index] =
        clamp_s16(right);
}
    return ESP_OK;
}