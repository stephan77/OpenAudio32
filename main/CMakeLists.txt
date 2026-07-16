idf_component_register(
    SRCS
        "main.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        audio
        network
        streamer
        esp_psram
        nvs_flash
)