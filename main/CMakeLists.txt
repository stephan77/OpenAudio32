idf_component_register(
    SRCS
        "main.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        audio
        network
        streamer
        web
        stations
        settings
        nvs_flash
        spotify
)