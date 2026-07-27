idf_component_register(
    SRCS
        "audio_output_adapter.c"

    INCLUDE_DIRS
        "."
        "../../external/airplay-esp32/main/audio"

    REQUIRES
        audio
        freertos
        log
)