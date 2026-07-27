idf_component_register(
    SRCS
        "audio.c"
        "wav_player.c"
        "audio_dsp.c"

    INCLUDE_DIRS
        "."
        "include"
    REQUIRES
        esp_driver_i2s
        esp_ringbuf
        freertos

    EMBED_FILES
        "assets/test.wav"
)