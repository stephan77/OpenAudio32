idf_component_register(
    SRCS
        "streamer.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        audio
        esp_http_client
)