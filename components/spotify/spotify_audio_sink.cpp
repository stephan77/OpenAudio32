#include "esp_log.h"

static const char *TAG = "spotify_sink";

extern "C" void spotify_audio_sink_placeholder(void)
{
    ESP_LOGD(
        TAG,
        "Spotify Audio-Sink noch nicht aktiviert"
    );
}