idf_component_register(
    SRCS
        "audio.c"
        "wav_player.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        esp_driver_i2s
        esp_ringbuf
        freertos

    EMBED_FILES
        "assets/test.wav"
)